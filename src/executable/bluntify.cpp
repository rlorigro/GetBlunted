#include "AdjacencyComponent.hpp"
#include "BicliqueCover.hpp"
#include "OverlapMap.hpp"
#include "gfa_to_handle.hpp"
#include "handle_to_gfa.hpp"
#include "duplicate_terminus.hpp"
#include "copy_graph.hpp"
#include "utility.hpp"

#include "bdsg/hash_graph.hpp"

using bluntifier::gfa_to_path_handle_graph_in_memory;
using bluntifier::gfa_to_path_handle_graph;
using bluntifier::gfa_to_handle_graph;
using bluntifier::handle_graph_to_gfa;
using bluntifier::parent_path;
using bluntifier::join_paths;
using bluntifier::IncrementalIdMap;
using bluntifier::OverlapMap;
using bluntifier::Alignment;
using bluntifier::for_each_adjacency_component;
using bluntifier::AdjacencyComponent;
using bluntifier::BipartiteGraph;
using bluntifier::BicliqueCover;
using bluntifier::bipartition;
using bluntifier::copy_path_handle_graph;
using bluntifier::duplicate_prefix;
using bluntifier::duplicate_suffix;
using bluntifier::run_command;

using handlegraph::MutablePathDeletableHandleGraph;
using handlegraph::as_integer;
using handlegraph::handle_t;
using bdsg::HashGraph;


class BicliqueEdgeIndex{
public:
    size_t biclique_index;
    size_t edge_index;

    BicliqueEdgeIndex(size_t biclique, size_t edge);
};


BicliqueEdgeIndex::BicliqueEdgeIndex(size_t biclique, size_t edge):
    biclique_index(biclique),
    edge_index(edge)
{}


class Bicliques{
public:
    vector <vector <edge_t> > bicliques;

    edge_t& operator[](BicliqueEdgeIndex i);
    const edge_t& operator[](BicliqueEdgeIndex i) const;

    vector <edge_t>& operator[](size_t i);
    const vector <edge_t>& operator[](size_t i) const;

    size_t size() const;
};


size_t Bicliques::size() const{
    return bicliques.size();
}


edge_t& Bicliques::operator[](BicliqueEdgeIndex i){
    return bicliques[i.biclique_index][i.edge_index];
}


const edge_t& Bicliques::operator[](BicliqueEdgeIndex i) const{
    return bicliques[i.biclique_index][i.edge_index];
}


vector <edge_t>& Bicliques::operator[](size_t i){
    return bicliques[i];
}


const vector <edge_t>& Bicliques::operator[](size_t i) const{
    return bicliques[i];
}


class OverlapInfo{
public:
    size_t edge_index;
    size_t length;

    OverlapInfo(size_t edge_index, size_t length);
};


OverlapInfo::OverlapInfo(size_t edge_index, size_t length) :
    edge_index(edge_index),
    length(length)
{}


class NodeInfo{
public:
    array <map <size_t, vector <OverlapInfo> >, 2> factored_overlaps;
    const vector <vector <BicliqueEdgeIndex> >& node_to_biclique_edge;
    const Bicliques& bicliques;
    const HandleGraph& gfa_graph;
    const OverlapMap& overlaps;
    const size_t node_id;

    NodeInfo(
            const vector <vector <BicliqueEdgeIndex> >& node_to_biclique_edge,
            const Bicliques& bicliques,
            const HandleGraph& gfa_graph,
            const OverlapMap& overlaps,
            size_t node_id);

    void factor_overlaps_by_biclique_and_side();
    void sort_factored_overlaps();

    size_t get_overlap_length(edge_t edge, bool side);
    void get_sorted_biclique_extents(
            array <deque <size_t>, 2>& sorted_extents_per_side,
            array <deque <size_t>, 2>& sorted_bicliques_per_side);

    void print_stats();
};


NodeInfo::NodeInfo(
        const vector <vector <BicliqueEdgeIndex> >& node_to_biclique_edge,
        const Bicliques& bicliques,
        const HandleGraph& gfa_graph,
        const OverlapMap& overlaps,
        size_t node_id):
    node_to_biclique_edge(node_to_biclique_edge),
    bicliques(bicliques),
    gfa_graph(gfa_graph),
    overlaps(overlaps),
    node_id(node_id)
{
    factor_overlaps_by_biclique_and_side();
    sort_factored_overlaps();
}


void NodeInfo::print_stats() {
    cout << "Node " << node_id << '\n';

    for (size_t side: {0,1}){
        auto biclique_overlaps = factored_overlaps[side];

        cout << "  Side " << side << '\n';

        for (const auto& item: biclique_overlaps){
            auto biclique_index = item.first;
            auto overlap_infos = item.second;

            cout << "    Biclique " << biclique_index << '\n';

            for (const auto& overlap_info: overlap_infos){
                cout << "      " << overlap_info.edge_index << " " << overlap_info.length << '\n';
            }
        }

        cout << '\n';
    }

    cout << '\n';
}


size_t NodeInfo::get_overlap_length(edge_t edge, bool side){
    pair<size_t, size_t> lengths;
    overlaps.at(edge)->second.compute_lengths(lengths);

    size_t length;
    if (side == 0){
        length = lengths.first;
    }
    else{
        length = lengths.second;
    }

    return length;
}


// For one node, make a mapping: (side -> (biclique_index -> (edge_index,length) ) )
void NodeInfo::factor_overlaps_by_biclique_and_side() {

    for (auto& index: node_to_biclique_edge[node_id]) {
        auto edge = bicliques[index];

        auto left_node_id = gfa_graph.get_id(edge.first);
        auto right_node_id = gfa_graph.get_id(edge.second);

        // It's possible that the edge is a self-edge. Add the edge (index) to any side that it matches.
        // Also, if the node is on the "left" of an edge then the overlap happens on the "right side" of the node...
        if (left_node_id == nid_t(node_id)) {
            auto length = get_overlap_length(edge, 0);
            factored_overlaps[1][index.biclique_index].emplace_back(index.edge_index, length);
        }
        if (right_node_id == nid_t(node_id)) {
            auto length = get_overlap_length(edge, 1);
            factored_overlaps[0][index.biclique_index].emplace_back(index.edge_index, length);
        }
    }
}


void NodeInfo::sort_factored_overlaps(){
    // First sort each biclique by its constituent edges
    for (bool side: {0,1}) {
        auto& biclique_edge_indexes = factored_overlaps[side];

        for (auto& biclique: biclique_edge_indexes) {
            auto& biclique_index = biclique.first;
            auto& overlap_infos = biclique.second;

            sort(overlap_infos.begin(), overlap_infos.end(), [&](OverlapInfo a, OverlapInfo b){
                return a.length > b.length;
            });
        }
    }
}


void NodeInfo::get_sorted_biclique_extents(
        array <deque <size_t>, 2>& sorted_extents_per_side,
        array <deque <size_t>, 2>& sorted_bicliques_per_side){

    sorted_extents_per_side = {};
    sorted_bicliques_per_side = {};

    for (auto side: {0,1}) {
        vector <pair <size_t, size_t> > sorted_biclique_extents;

        auto& biclique_edge_indexes = factored_overlaps[side];

        // Collect all the longest overlaps for each biclique (NodeInfo keeps them in descending sorted order)
        for (auto& biclique: biclique_edge_indexes) {
            auto& biclique_index = biclique.first;
            auto& overlap_infos = biclique.second;

            sorted_biclique_extents.emplace_back(biclique_index, overlap_infos[0].length);
        }

        // Sort the bicliques by their longest overlap length
        sort(sorted_biclique_extents.begin(), sorted_biclique_extents.end(),
             [&](const pair<size_t, size_t>& a, const pair<size_t, size_t>& b) {
                 return a.second > b.second;
             });

        // Unzip the pairs into 2 deques (makes it easier to send the data off to the recursive duplicator)
        for (auto& item: sorted_biclique_extents){
            cout << item.first << " " << item.second << '\n';
            sorted_bicliques_per_side[side].emplace_back(item.first);
            sorted_extents_per_side[side].emplace_back(item.second);
        }
    }
}


void deduplicate_and_canonicalize_biclique_cover(
        vector <bipartition>& biclique_cover,
        vector <vector <edge_t> >& deduplicated_biclique_cover,
        const HandleGraph& gfa_graph,
        const OverlapMap& overlaps){

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


void compute_all_bicliques(
        size_t i,
        const HashGraph& gfa_graph,
        const OverlapMap& overlaps,
        vector<AdjacencyComponent>& adjacency_components,
        Bicliques& bicliques,
        mutex& biclique_mutex){

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
                deduplicated_biclique_cover,
                gfa_graph,
                overlaps);

        for (auto& biclique: deduplicated_biclique_cover) {
            biclique_mutex.lock();
            bicliques.bicliques.emplace_back(biclique);
            biclique_mutex.unlock();
        }
    });
}


void update_biclique_edges(
        MutablePathMutableHandleGraph& gfa_graph,
        Bicliques& bicliques,
        OverlapMap& overlaps,
        nid_t old_node_id,
        handle_t old_handle,
        handle_t old_handle_flipped,
        const array <deque <size_t>, 2>& sorted_bicliques_per_side,
        const deque <handle_t>& children,
        bool duped_side){

    for (auto& item: children) {
        std::cout << gfa_graph.get_id(item) << " " << as_integer(item) << "F " << as_integer(gfa_graph.flip(item)) << "R " << gfa_graph.get_sequence(item) << '\n';
    }

    for (bool side: {0,1}) {
        for (size_t i = 0; i < sorted_bicliques_per_side[side].size(); i++) {
            size_t biclique_index = sorted_bicliques_per_side[side][i];

            for (auto& edge: bicliques[biclique_index]) {
                auto old_edge = edge;

                cout << "Replacing " << old_node_id << '\n';
                cout << "Replacing " << as_integer(old_handle) <<"F or " << as_integer(old_handle_flipped) << "R" << '\n';

                cout << "[" << biclique_index << "] " << duped_side << " " << side << as_integer(old_edge.first) << "h->" << as_integer(old_edge.second) << "h" << '\n';

                if (duped_side == 0) {
                    if (side == 0) {
                        if (old_edge.second == old_handle){
                            edge.second = children[i+1];

                            // Account for self loops (non-reversing)
                            if (old_edge.first == old_edge.second){
                                edge.first = children[0];
                            }
                            cout << "Creating " << gfa_graph.get_id(edge.first) << "->" << gfa_graph.get_id(edge.second) << '\n';
                            gfa_graph.create_edge(edge);
                            overlaps.update_edge(old_edge, edge);

                        }
                        else if (old_edge.second == old_handle_flipped){
                            edge.second = gfa_graph.flip(children[0]);

                            // Account for self loops (non-reversing)
                            if (old_edge.first == old_edge.second){
                                edge.first = gfa_graph.flip(children[i+1]);
                            }
                            cout << "Creating " << gfa_graph.get_id(edge.first) << "->" << gfa_graph.get_id(edge.second) << '\n';
                            gfa_graph.create_edge(edge);
                            overlaps.update_edge(old_edge, edge);

                        }
                    }
                    else{
                        if (old_edge.first == old_handle){
                            edge.first = children[0];

                            // Account for self loops (non-reversing)
                            if (old_edge.first == old_edge.second){
                                edge.second = children[i+1];
                            }
                            cout << "Creating " << gfa_graph.get_id(edge.first) << "->" << gfa_graph.get_id(edge.second) << '\n';
                            gfa_graph.create_edge(edge);
                            overlaps.update_edge(old_edge, edge);
                        }
                        else if (old_edge.first == old_handle_flipped){
                            edge.first = gfa_graph.flip(children[i+1]);

                            // Account for self loops (non-reversing)
                            if (old_edge.first == old_edge.second){
                                edge.second = gfa_graph.flip(children[0]);
                            }
                            cout << "Creating " << gfa_graph.get_id(edge.first) << "->" << gfa_graph.get_id(edge.second) << '\n';
                            gfa_graph.create_edge(edge);
                            overlaps.update_edge(old_edge, edge);
                        }
                    }
                }
                else{
                    if (side == 0) {
                        if (old_edge.second == old_handle){
                            edge.second = children[0];

                            // Account for self loops (non-reversing)
                            if (old_edge.first == old_edge.second){
                                edge.first = children[i+1];
                            }
                            cout << "Creating " << gfa_graph.get_id(edge.first) << "->" << gfa_graph.get_id(edge.second) << '\n';
                            gfa_graph.create_edge(edge);
                            overlaps.update_edge(old_edge, edge);

                        }
                        else if (old_edge.second == old_handle_flipped){
                            edge.second = gfa_graph.flip(children[i+1]);

                            // Account for self loops (non-reversing)
                            if (old_edge.first == old_edge.second){
                                edge.first = gfa_graph.flip(children[0]);
                            }
                            cout << "Creating " << gfa_graph.get_id(edge.first) << "->" << gfa_graph.get_id(edge.second) << '\n';
                            gfa_graph.create_edge(edge);
                            overlaps.update_edge(old_edge, edge);

                        }
                    }
                    else{
                        if (old_edge.first == old_handle){
                            edge.first = children[i+1];

                            // Account for self loops (non-reversing)
                            if (old_edge.first == old_edge.second){
                                edge.second = children[0];
                            }
                            cout << "Creating " << gfa_graph.get_id(edge.first) << "->" << gfa_graph.get_id(edge.second) << '\n';
                            gfa_graph.create_edge(edge);
                            overlaps.update_edge(old_edge, edge);

                        }
                        else if (old_edge.first == old_handle_flipped){
                            edge.first = gfa_graph.flip(children[0]);

                            // Account for self loops (non-reversing)
                            if (old_edge.first == old_edge.second){
                                edge.second = gfa_graph.flip(children[i+1]);
                            }
                            cout << "Creating " << gfa_graph.get_id(edge.first) << "->" << gfa_graph.get_id(edge.second) << '\n';
                            gfa_graph.create_edge(edge);
                            overlaps.update_edge(old_edge, edge);

                        }
                    }
                }
            }
        }
    }
    cout << '\n';
}


void remove_participating_edges(
        MutablePathDeletableHandleGraph& gfa_graph,
        Bicliques& bicliques,
        const array <deque <size_t>, 2>& sorted_bicliques_per_side,
        nid_t parent_node
){

    for (bool side: {0,1}) {
        for (auto& biclique_index: sorted_bicliques_per_side[side]) {
            for (auto& edge: bicliques[biclique_index]) {
                if (gfa_graph.get_id(edge.first) == parent_node or gfa_graph.get_id(edge.second) == parent_node) {
//                    cout << "Deleting " << gfa_graph.get_id(edge.first) << "->" << gfa_graph.get_id(edge.second)
//                         << '\n';
                    gfa_graph.destroy_edge(edge);
                }
            }
        }
    }

}


void duplicate_termini(
        const vector <vector <BicliqueEdgeIndex> >& node_to_biclique_edge,
        Bicliques& bicliques,
        MutablePathDeletableHandleGraph& gfa_graph,
        OverlapMap& overlaps){

    for (size_t node_id=1; node_id<node_to_biclique_edge.size(); node_id++){
        // Factor the overlaps into hierarchy: side -> biclique -> (overlap, length)
        NodeInfo node_info(node_to_biclique_edge, bicliques, gfa_graph, overlaps, node_id);

        node_info.print_stats();

        // Keep track of which biclique is in which position once sorted
        array <deque <size_t>, 2> sorted_sizes_per_side;
        array <deque <size_t>, 2> sorted_bicliques_per_side;

        node_info.get_sorted_biclique_extents(sorted_sizes_per_side, sorted_bicliques_per_side);

        {
            string test_path_prefix = "test_bluntify_" + std::to_string(node_id) + "_";
            handle_graph_to_gfa(gfa_graph, test_path_prefix + ".gfa");
            string command = "vg convert -g " + test_path_prefix + ".gfa -p | vg view -d - | dot -Tpng -o "
                             + test_path_prefix + ".png";
            run_command(command);
        }

        handle_t parent_handle = gfa_graph.get_handle(node_id, 0);
        handle_t parent_handle_flipped = gfa_graph.flip(parent_handle);
        nid_t parent_node = node_id;

        remove_participating_edges(gfa_graph, bicliques, sorted_bicliques_per_side, parent_node);

        deque<handle_t> left_children;
        deque<handle_t> right_children;

        if (not sorted_sizes_per_side[0].empty()) {
            duplicate_prefix(gfa_graph, sorted_sizes_per_side[0], left_children, parent_handle);

            update_biclique_edges(
                    gfa_graph,
                    bicliques,
                    overlaps,
                    parent_node,
                    parent_handle,
                    parent_handle_flipped,
                    sorted_bicliques_per_side,
                    left_children,
                    0);

            parent_handle = left_children.front();
            parent_handle_flipped = gfa_graph.flip(parent_handle);
            parent_node = gfa_graph.get_id(parent_handle);

            {
                string test_path_prefix = "test_bluntify_" + std::to_string(node_id) + "_" + std::to_string(0);
                handle_graph_to_gfa(gfa_graph, test_path_prefix + ".gfa");
                string command = "vg convert -g " + test_path_prefix + ".gfa -p | vg view -d - | dot -Tpng -o "
                                 + test_path_prefix + ".png";
                run_command(command);
            }
        }


        if (not sorted_sizes_per_side[1].empty()) {
            // Skip trivial duplication
            if (sorted_sizes_per_side[1].size() == 1 and sorted_sizes_per_side[1][0] == gfa_graph.get_length(parent_handle)){
//                cout << "Skipping trivial duplication\n";
                continue;
            }

            duplicate_suffix(gfa_graph, sorted_sizes_per_side[1], right_children, parent_handle);

            update_biclique_edges(
                    gfa_graph,
                    bicliques,
                    overlaps,
                    parent_node,
                    parent_handle,
                    parent_handle_flipped,
                    sorted_bicliques_per_side,
                    right_children,
                    1);

            {
                string test_path_prefix = "test_bluntify_" + std::to_string(node_id) + "_" + std::to_string(1);
                handle_graph_to_gfa(gfa_graph, test_path_prefix + ".gfa");
                string command = "vg convert -g " + test_path_prefix + ".gfa -p | vg view -d - | dot -Tpng -o "
                                 + test_path_prefix + ".png";
                run_command(command);
            }
        }
    }
}


void map_splice_sites_by_node(
        const HandleGraph& gfa_graph,
        const Bicliques& bicliques,
        vector <vector <BicliqueEdgeIndex> >& node_to_biclique_edge){

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
            node_to_biclique_edge[right_node_id].emplace_back(i,j);
        }
    }
}


void print_adjacency_components_stats(
        size_t i,
        vector<AdjacencyComponent>& adjacency_components,
        IncrementalIdMap<string>& id_map,
        HandleGraph& gfa_graph){

    cout << "Component " << i << " of size " << adjacency_components[i].size() << '\n' << std::flush;
    cout << "NODES IN ADJACENCY COMPONENT:\n";
    for (auto& handle: adjacency_components[i]) {
        std::cout << id_map.get_name(gfa_graph.get_id(handle)) << (gfa_graph.get_is_reverse(handle) ? "-" : "+")
                  << '\n';
    }
    cout << '\n';
}


void bluntify(string gfa_path){
    ifstream file(gfa_path);

    HashGraph gfa_graph;
    IncrementalIdMap<string> id_map;
    OverlapMap overlaps;

    gfa_to_handle_graph(gfa_path, gfa_graph, id_map, overlaps);

    {
        size_t id = 1;
        for (auto& item: id_map.names) {
            cout << id++ << " " << item << '\n';
        }
    }

    // Where all the ACs go
    vector<AdjacencyComponent> adjacency_components;

    // Compute Adjacency Components and store in vector
    compute_all_adjacency_components(gfa_graph, adjacency_components);

    // Where all the Bicliques go (once we have these, no longer need Adjacency Components)
    Bicliques bicliques;
    mutex biclique_mutex;

    auto size = gfa_graph.get_node_count() + 1;
    vector <vector <BicliqueEdgeIndex> > node_to_biclique_edge(size);

    std::cout << "Total adjacency components:\t" << adjacency_components.size() << '\n' << '\n';

    for (size_t i = 0; i<adjacency_components.size(); i++){
        print_adjacency_components_stats(i,adjacency_components,id_map,gfa_graph);
        compute_all_bicliques(i, gfa_graph, overlaps, adjacency_components, bicliques, biclique_mutex);
    }

    // TODO: delete adjacency components

    map_splice_sites_by_node(gfa_graph, bicliques, node_to_biclique_edge);

    duplicate_termini(node_to_biclique_edge, bicliques, gfa_graph, overlaps);


}


int main(int argc, char **argv){
    string gfa_path;

    if (argc == 1){
        throw runtime_error("No input gfa path provided");
    }
    else if (argc == 2){
        gfa_path = argv[1];
    }
    else{
        throw runtime_error("Too many arguments. Specify 1 input gfa path.");
    }

    bluntify(gfa_path);

    return 0;
}

