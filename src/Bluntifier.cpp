#include "Bluntifier.hpp"

using std::to_string;

namespace bluntifier{


ProvenanceInfo::ProvenanceInfo(size_t start, size_t stop, bool reversal):
    start(start),
    stop(stop),
    reversal(reversal)
{}


Bluntifier::Bluntifier(string gfa_path):
    gfa_path(gfa_path)
{}


void Bluntifier::print_adjacency_components_stats(size_t i){
    cout << "Component " << i << " of size " << adjacency_components[i].size() << '\n' << std::flush;
    cout << "NODES IN ADJACENCY COMPONENT:\n";
    for (auto& handle: adjacency_components[i]) {
        std::cout << id_map.get_name(gfa_graph.get_id(handle)) << (gfa_graph.get_is_reverse(handle) ? "-" : "+")
                  << '\n';
    }
    cout << '\n';
}


void Bluntifier::deduplicate_and_canonicalize_biclique_cover(
        vector <bipartition>& biclique_cover,
        vector <vector <edge_t> >& deduplicated_biclique_cover){

    // sort the bicliques in descending order by size (to get any repeated edges
    // into larger POAs -- likely to be more compact this way)
    sort(biclique_cover.begin(), biclique_cover.end(),
         [&](const bipartition& a, const bipartition& b) {
             return a.first.size() * a.second.size() > b.first.size() * b.second.size();
         });

    unordered_set<edge_t> edges_processed;
    for (const bipartition& biclique : biclique_cover) {
        deduplicated_biclique_cover.emplace_back();

        // get the edges that haven't been handled in a previous biclique
        for (handle_t left : biclique.first) {
            for (handle_t right : biclique.second) {
                edge_t edge(left, gfa_graph.flip(right));
                auto iter = overlaps.canonicalize_and_find(edge, gfa_graph);

                if (!edges_processed.count(edge)) {
                    edges_processed.emplace(edge);
                    deduplicated_biclique_cover.back().emplace_back(iter->first);
                }
            }
        }
    }
}


void Bluntifier::compute_biclique_cover(size_t i){
    // TODO: switch to fetch_add atomic
    auto& adjacency_component = adjacency_components[i];

    // Skip trivial adjacency components (dead ends)
    if (adjacency_component.size() == 1) {
        return;
    }

    adjacency_component.decompose_into_bipartite_blocks([&](const BipartiteGraph& bipartite_graph){
        vector <bipartition> biclique_cover = BicliqueCover(bipartite_graph).get();
        vector <vector <edge_t> > deduplicated_biclique_cover;

        // TODO: find a lock-minimal thread safe way to prevent copying each biclique cover during duplication
        // TODO: Maybe just move the deduplication step outside of thread fn?
        deduplicate_and_canonicalize_biclique_cover(
                biclique_cover,
                deduplicated_biclique_cover);

        for (auto& biclique: deduplicated_biclique_cover) {
            biclique_mutex.lock();
            bicliques.bicliques.emplace_back(biclique);
            biclique_mutex.unlock();
        }
    });
}


void Bluntifier::map_splice_sites_by_node(){

    // Create a mapping from all the nodes to their participating edges in each biclique, where the mapping
    // just keeps track of the biclique index and the intra-biclique index for each edge in the
    // "bicliques" vector of vectors, using a pair of indexes {bc_index, ibc_index}
    for (size_t i=0; i<bicliques.size(); i++){
        for (size_t j=0; j<bicliques[i].size(); j++){
            const auto& edge = bicliques[i][j];

            nid_t left_node_id;
            nid_t right_node_id;

            left_node_id = gfa_graph.get_id(edge.first);
            right_node_id = gfa_graph.get_id(edge.second);

            node_to_biclique_edge[left_node_id].emplace_back(i,j);

            // Don't make 2 mappings ot he same edge if it is a self-loop
            if (right_node_id != left_node_id) {
                node_to_biclique_edge[right_node_id].emplace_back(i, j);
            }
        }
    }
}


bool Bluntifier::is_oo_node_child(nid_t node_id){
    bool is_oo = false;

    // Check if this is an Overlapping Overlap node
    auto is_child_result = child_to_parent.find(node_id);

    if (is_child_result != child_to_parent.end()){
        auto& original_gfa_node = is_child_result->second;

        auto result = overlapping_overlap_nodes.find(original_gfa_node);
        if (result != overlapping_overlap_nodes.end()) {
            // Brute force search of overlapping children in this OO node
            for (auto s: {0,1}) {
                for (auto& oo_item: result->second.overlapping_children[s]) {
                    if (gfa_graph.get_id(oo_item.second.handle) == node_id){
                        cout << "Skipping OO node: " << original_gfa_node << '\n';
                        is_oo = true;
                    }
                }
            }
        }
    }


    return is_oo;
}


bool Bluntifier::is_oo_node_parent(nid_t node_id){

    bool is_oo = false;

    // Check if this is an Overlapping Overlap node
    auto is_child_result = child_to_parent.find(node_id);

    if (is_child_result != child_to_parent.end()) {
        auto& original_gfa_node = is_child_result->second;

        auto result = overlapping_overlap_nodes.find(original_gfa_node);
        if (result != overlapping_overlap_nodes.end()) {

            auto parent_path = gfa_graph.get_path_handle(result->second.parent_path_name);

            for (auto h: gfa_graph.scan_path(parent_path)) {
                if (gfa_graph.get_id(h) == node_id) {
                    is_oo = true;
                }
            }
        }
    }

    return is_oo;
}


void Bluntifier::splice_subgraphs(){

    cout << "Splicing " << subgraphs.size() << " subgraphs\n";

    size_t i = 0;
    for (auto& subgraph: subgraphs) {

        // First, copy the subgraph into the GFA graph
        subgraph.graph.increment_node_ids(gfa_graph.max_node_id());
        copy_path_handle_graph(&subgraph.graph, &gfa_graph);

        if (gfa_graph.get_node_count() < 30){
            string test_path_prefix = "test_bluntify_splice_" + std::to_string(i) + "_b";
            handle_graph_to_gfa(gfa_graph, test_path_prefix + ".gfa");
            string command = "vg convert -g " + test_path_prefix + ".gfa -p | vg view -d - | dot -Tpng -o "
                             + test_path_prefix + ".png";
            run_command(command);
        }

        i++;

        // Iterate the suffixes/prefixes that participated in this biclique
        for (bool side: {0, 1}) {
            for (auto& item: subgraph.paths_per_handle[side]) {
                auto& handle = item.first;
                auto& path_info = item.second;
                auto node_id = gfa_graph.get_id(handle);

//                cout << "Splicing node " << node_id << ", side " << side << '\n';

                bool is_oo_child = is_oo_node_child(node_id);
                bool is_oo_parent = is_oo_node_parent(node_id);

                if (not is_oo_child) {

                    // Find the path handle for the path that was copied into the GFA graph
                    auto path_name = subgraph.graph.get_path_name(path_info.path_handle);
                    auto path_handle = gfa_graph.get_path_handle(path_name);

                    set<handle_t> parent_handles;
                    gfa_graph.follow_edges(handle, 1 - side, [&](const handle_t& h) {
                        if (to_be_destroyed.count(gfa_graph.get_id(h)) == 0) {
                            parent_handles.emplace(h);
                        }
                    });

                    if (parent_handles.empty() and not is_oo_parent) {
                        throw runtime_error("ERROR: biclique terminus does not have any parent: " + to_string(node_id));
                    }

                    for (auto& parent_handle: parent_handles) {
                        // Depending on which side of the biclique this node is on, its path in the POA will be spliced
                        // differently
                        if (path_info.biclique_side == 0) {
                            auto& left = parent_handle;
                            auto right = gfa_graph.get_handle_of_step(gfa_graph.path_begin(path_handle));

                            gfa_graph.create_edge(left, right);
                        } else {
                            auto left = gfa_graph.get_handle_of_step(gfa_graph.path_back(path_handle));
                            auto& right = parent_handle;

                            gfa_graph.create_edge(left, right);
                        }
                    }
                }
                else{
                    cout << "Skipping oo child: " << node_id << '\n';
                }

                if (subgraph.paths_per_handle[1-side].count(handle) == 0
                    and subgraph.paths_per_handle[1-side].count(gfa_graph.flip(handle)) == 0) {
                    cout << "To be destroyed: " << gfa_graph.get_id(handle) << '\n';
                    to_be_destroyed.emplace(gfa_graph.get_id(handle));
                }
            }
        }
    }
}



void Bluntifier::bluntify(){
    gfa_to_handle_graph(gfa_path, gfa_graph, id_map, overlaps);

//    {
//        size_t id = 1;
//        for (auto& item: id_map.names) {
//            cout << id++ << " " << item << '\n';
//        }
//    }

    // Compute Adjacency Components and store in vector
    compute_all_adjacency_components(gfa_graph, adjacency_components);

    // Where all the Bicliques go (once we have these, no longer need Adjacency Components)
    auto size = gfa_graph.get_node_count() + 1;
    node_to_biclique_edge.resize(size);

    std::cout << "Total adjacency components:\t" << adjacency_components.size() << '\n' << '\n';

    for (size_t i = 0; i<adjacency_components.size(); i++){
        print_adjacency_components_stats(i);
        compute_biclique_cover(i);
    }

    {
        size_t i = 0;
        for (auto& biclique: bicliques.bicliques) {
            cout << "Biclique " << i++ << '\n';
            for (auto& edge: biclique) {
                cout << "(" << gfa_graph.get_id(edge.first);
                cout << (gfa_graph.get_is_reverse(edge.first) ? "-" : "+");
                cout << ") -> (" << gfa_graph.get_id(edge.second);
                cout << (gfa_graph.get_is_reverse(edge.second) ? "-" : "+") << ")" << '\n';
            }
        }
        cout << '\n' << '\n';
    }

    // TODO: delete adjacency components vector if unneeded

    map_splice_sites_by_node();

    Duplicator super_duper(
            node_to_biclique_edge,
            overlaps,
            bicliques,
            parent_to_children,
            child_to_parent,
            overlapping_overlap_nodes);

    if (gfa_graph.get_node_count() < 30){
        string test_path_prefix = "test_bluntify_" + std::to_string(0);
        handle_graph_to_gfa(gfa_graph, test_path_prefix + ".gfa");
        string command = "vg convert -g " + test_path_prefix + ".gfa -p | vg view -d - | dot -Tpng -o "
                         + test_path_prefix + ".png";
        run_command(command);
    }

    super_duper.duplicate_all_node_termini(gfa_graph);

    if (gfa_graph.get_node_count() < 30){
        string test_path_prefix = "test_bluntify_" + std::to_string(1);
        handle_graph_to_gfa(gfa_graph, test_path_prefix + ".gfa");
        string command = "vg convert -g " + test_path_prefix + ".gfa -p | vg view -d - | dot -Tpng -o "
                         + test_path_prefix + ".png";
        run_command(command);
    }

    harmonize_biclique_orientations();

    subgraphs.resize(bicliques.size());

    for (size_t i=0; i<bicliques.size(); i++){
        align_biclique_overlaps(i);
    }

    splice_subgraphs();

    if (gfa_graph.get_node_count() < 200){
        string test_path_prefix = "test_bluntify_spliced_" + std::to_string(1);
        handle_graph_to_gfa(gfa_graph, test_path_prefix + ".gfa");
        string command = "vg convert -g " + test_path_prefix + ".gfa -p | vg view -d - | dot -Tpng -o "
                         + test_path_prefix + ".png";
        run_command(command);
    }

    OverlappingOverlapSplicer oo_splicer(overlapping_overlap_nodes, parent_to_children, subgraphs);

    oo_splicer.splice_overlapping_overlaps(gfa_graph);

    if (gfa_graph.get_node_count() < 200){
        string test_path_prefix = "test_bluntify_spliced_oo_" + std::to_string(1);
        handle_graph_to_gfa(gfa_graph, test_path_prefix + ".gfa");
        string command = "vg convert -g " + test_path_prefix + ".gfa -p | vg view -d - | dot -Tpng -o "
                         + test_path_prefix + ".png";

        cerr << "Running command: " << command << '\n';
        run_command(command);
    }

    compute_provenance();

    string provenance_log_path = "test_bluntify_provenance.txt";
    write_provenance(provenance_log_path);

    for (auto& id: to_be_destroyed){
        // TODO: remove node from provenance map
        gfa_graph.destroy_handle(gfa_graph.get_handle(id));
    }

    string test_path_prefix = "test_bluntify_final";
    handle_graph_to_gfa(gfa_graph, test_path_prefix + ".gfa");

    if (gfa_graph.get_node_count() < 200){
        string command = "vg convert -g " + test_path_prefix + ".gfa -p | vg view -d - | dot -Tpng -o "
                         + test_path_prefix + ".png";

        run_command(command);
    }
}


//void Bluntifier::find_path_info(
//        Subgraph& subgraph,
//        handle_t handle,
//        PathInfo& path_info,
//        string& path_name){
//
//    // Don't know which side of the biclique this overlap was on until we search for it in the subgraph
//    auto result = subgraph.paths_per_handle[0].find(handle);
//
//    if (result == subgraph.paths_per_handle[0].end()) {
//        result = subgraph.paths_per_handle[1].find(handle);
//
//        if (result == subgraph.paths_per_handle[1].end()) {
//            result = subgraph.paths_per_handle[0].find(gfa_graph.flip(handle));
//
//            if (result == subgraph.paths_per_handle[0].end()) {
//                result = subgraph.paths_per_handle[1].find(gfa_graph.flip(handle));
//
//                // Sanity check
//                if (result == subgraph.paths_per_handle[1].end()) {
//                    throw runtime_error("ERROR: node not found in biclique subgraph. Node id: " +
//                                        to_string(gfa_graph.get_id(handle)));
//                }
//                else{
//                    cout << "WARNING: handle flipped w.r.t. path in subgraph. Node id: " << gfa_graph.get_id(handle) << '\n';
//                }
//            }
//            else{
//                cout << "WARNING: handle flipped w.r.t. path in subgraph. Node id: " << gfa_graph.get_id(handle) << '\n';
//            }
//        }
//    }
//
//    path_name = subgraph.graph.get_path_name(result->second.path_handle);
//    path_info = result->second;
//}


void Bluntifier::write_provenance(string& output_path){
    ofstream file(output_path);

    for (auto& [child_node, parents]: provenance_map){
        file << child_node << '\t';

        auto iter = parents.begin();
        while (true){
            auto& parent_node = iter->first;
            auto& info = iter->second;

            file << parent_node << '[' << info.start << ':' << info.stop + 1 << "]" << (info.reversal ? '-':'+');

            if (++iter == parents.end()){
                break;
            }

            file << ',';
        }

        file << '\n';
    }
}


//void Bluntifier::find_child_provenance(
//        nid_t child_node,
//        nid_t parent_node_id,
//        Subgraph& subgraph,
//        size_t parent_index,
//        bool side){
//
//    auto handle = gfa_graph.get_handle(child_node, false);
//    PathInfo path_info;
//    string path_name;
//
//    find_path_info(subgraph, handle, path_info, path_name);
//    auto path_handle = gfa_graph.get_path_handle(path_name);
//
//    for (auto h: gfa_graph.scan_path(path_handle)){
//        auto id = gfa_graph.get_id(h);
//        size_t length = gfa_graph.get_length(h);
//
//        // Store the provenance info for this node if it's not a terminus
//        pair <size_t, size_t> info = {parent_index, parent_index + length - 1};
//        provenance_map[id].emplace(parent_node_id, info);
//
//        parent_index += length;
//    }
//}


void Bluntifier::compute_provenance(){
    gfa_graph.for_each_path_handle([&](const path_handle_t ph){
        cout << gfa_graph.get_path_name(ph) << '\n';
    });

    for (int64_t parent_node_id=1; parent_node_id <= id_map.names.size(); parent_node_id++){
        string parent_path_name = to_string(parent_node_id);
        auto parent_path_handle = gfa_graph.get_path_handle(parent_path_name);

        size_t i = 0;
        size_t parent_index = 0;
        size_t parent_length = 0;
        bool has_left_child = false;
        bool has_right_child = false;
        for (auto h: gfa_graph.scan_path(parent_path_handle)){
            auto id = gfa_graph.get_id(h);
            size_t length = gfa_graph.get_length(h);
            parent_length += length;

            // Check if this is a duplicated prefix/suffix
            if (child_to_parent.count(id) > 0){
                if (i == 0){
                    has_left_child = true;
                }
                else{
                    has_right_child = true;
                    break;
                }
            }
            // Store the provenance info for this node if it's not a terminus/child
            else if (to_be_destroyed.count(id) == 0){
                ProvenanceInfo info(parent_index, parent_index + length - 1, false);
                provenance_map[id].emplace(parent_node_id, info);
            }

            parent_index += length;

            i++;
        }


        // Waste some computation re-computing the factored overlaps per side for this node
        // But this is (mostly) necessary because the graph has been edited,
        // and biclique harmonization will have randomly flipped the edges
        NodeInfo node_info(
                node_to_biclique_edge,
                child_to_parent,
                bicliques,
                gfa_graph,
                overlaps,
                parent_node_id);

        node_info.print_stats();
        cout << has_left_child << has_right_child << '\n';

        for (size_t side: {0, 1}) {
            auto biclique_overlaps = node_info.factored_overlaps[side];

            for (const auto& item: biclique_overlaps) {
                auto biclique_index = item.first;
                auto overlap_infos = item.second;

                // Longest overlap defines this biclique
                auto& overlap_info = overlap_infos[0];
                edge_t& edge = bicliques[biclique_index][overlap_info.edge_index];
                edge_t canonical_edge = overlaps.canonicalize_and_find(edge, gfa_graph)->first;

                nid_t child_id;
                bool reversal;
                bool parent_side;

                if (child_to_parent.at(gfa_graph.get_id(canonical_edge.first)) == parent_node_id){
                    reversal = gfa_graph.get_is_reverse(canonical_edge.first);

                    if (reversal){
                        child_id = gfa_graph.get_id(canonical_edge.first);
                        parent_index = 0;

                        parent_side = 0;
                        if (canonical_edge != edge){
                            parent_side = 1 - parent_side;
                        }
                    }
                    else{
                        child_id = gfa_graph.get_id(canonical_edge.first);
                        parent_index = parent_length - overlap_info.length;

                        parent_side = 0;
                        if (canonical_edge != edge){
                            parent_side = 1 - parent_side;
                        }
                    }
                }
                else{
                    reversal = gfa_graph.get_is_reverse(canonical_edge.second);

                    if (reversal){
                        child_id = gfa_graph.get_id(canonical_edge.second);
                        parent_index = parent_length - overlap_info.length;

                        parent_side = 1;
                        if (canonical_edge != edge){
                            parent_side = 1 - parent_side;
                        }
                    }
                    else{
                        child_id = gfa_graph.get_id(canonical_edge.second);
                        parent_index = 0;

                        parent_side = 1;
                        if (canonical_edge != edge){
                            parent_side = 1 - parent_side;
                        }
                    }
                }


//                cout << "parent node: " << parent_node_id << '\n';
//                cout << "parent side: " << parent_side << '\n';
//                cout << "reversal: " << reversal << '\n';
//                cout << "parent length: " << parent_length << '\n';
//                cout << "overlap length: " << overlap_info.length << '\n';
//                cout << "parent index: " << parent_index << '\n';
//                cout << "(" << gfa_graph.get_id(edge.first);
//                cout << (gfa_graph.get_is_reverse(edge.first) ? "-" : "+");
//                cout << ") -> (" << gfa_graph.get_id(edge.second);
//                cout << (gfa_graph.get_is_reverse(edge.second) ? "-" : "+") << ")" << '\n';
//                cout << '\n';

                string child_path_name = to_string(child_id) + "_" + to_string(parent_side);
                auto child_path_handle = gfa_graph.get_path_handle(child_path_name);

                for (auto h: gfa_graph.scan_path(child_path_handle)){
                    auto id = gfa_graph.get_id(h);
                    size_t length = gfa_graph.get_length(h);

                    // Store the provenance info for this node
                    ProvenanceInfo info(parent_index, parent_index + length - 1, reversal);
                    provenance_map[id].emplace(parent_node_id, info);

                    parent_index += length;

                    i++;
                }

            }
        }
        cout << '\n';
    }
}



}