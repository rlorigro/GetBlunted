#include "NodeInfo.hpp"


namespace bluntifier{


OverlapInfo::OverlapInfo(size_t edge_index, size_t length) :
        edge_index(edge_index),
        length(length) {}


NodeInfo::NodeInfo(
        const vector<vector<BicliqueEdgeIndex> >& node_to_biclique_edge,
        const Bicliques& bicliques,
        const HandleGraph& gfa_graph,
        const OverlapMap& overlaps,
        nid_t node_id) :
        node_to_biclique_edge(node_to_biclique_edge),
        bicliques(bicliques),
        gfa_graph(gfa_graph),
        overlaps(overlaps),
        node_id(node_id) {

    factor_overlaps_by_biclique_and_side();
    sort_factored_overlaps();
}


NodeInfo::NodeInfo(
        const vector<vector<BicliqueEdgeIndex> >& node_to_biclique_edge,
        const map <nid_t, pair<nid_t, bool> >& child_to_parent,
        const Bicliques& bicliques,
        const HandleGraph& gfa_graph,
        const OverlapMap& overlaps,
        nid_t node_id) :
        node_to_biclique_edge(node_to_biclique_edge),
        bicliques(bicliques),
        gfa_graph(gfa_graph),
        overlaps(overlaps),
        node_id(node_id) {

    factor_overlaps_by_biclique_and_side(child_to_parent);
    sort_factored_overlaps();
}


void NodeInfo::print_stats() const{
    cerr << "Node " << node_id << '\n';

    for (size_t side: {0, 1}) {
        auto biclique_overlaps = factored_overlaps[side];

        cerr << "  Side " << side << '\n';

        for (const auto& item: biclique_overlaps) {
            auto biclique_index = item.first;
            auto overlap_infos = item.second;

            cerr << "    Biclique " << biclique_index << '\n';

            for (const auto& overlap_info: overlap_infos) {
                auto edge = bicliques[biclique_index][overlap_info.edge_index];
                auto left_id = gfa_graph.get_id(edge.first);
                auto right_id = gfa_graph.get_id(edge.second);
                cerr << "      " << overlap_info.edge_index << " " << overlap_info.length << " " << left_id << "->"
                     << right_id << '\n';
            }
        }

        cerr << '\n';
    }

    cerr << '\n';
}


size_t NodeInfo::get_overlap_length(edge_t edge, bool side) {
    pair<size_t, size_t> lengths;
    overlaps.at(edge)->second.compute_lengths(lengths);

    size_t length;
    if (side == 0) {
        length = lengths.first;
    } else {
        length = lengths.second;
    }

    return length;
}


// For one node, make a mapping: (side -> (biclique_index -> (edge_index,length) ) )
// Overloaded to find overlaps that involve the original parent node if the graph has been edited
void NodeInfo::factor_overlaps_by_biclique_and_side(const map <nid_t, pair<nid_t, bool> >& child_to_parent) {

    for (auto& index: node_to_biclique_edge[node_id]) {
        edge_t edge = bicliques[index];
        edge = overlaps.canonicalize_and_find(edge, gfa_graph)->first;

        // Parent node needs to be found if it exists
        nid_t left_node_id = gfa_graph.get_id(edge.first);
        auto left_parent_node_iter = child_to_parent.find(left_node_id);

        if (left_parent_node_iter !=  child_to_parent.end()){
            left_node_id = left_parent_node_iter->second.first;
        }

        nid_t right_node_id = gfa_graph.get_id(edge.second);
        auto right_parent_node_iter = child_to_parent.find(right_node_id);

        if (right_parent_node_iter !=  child_to_parent.end()){
            right_node_id = right_parent_node_iter->second.first;
        }

        // If the node is on the "left" of an edge then the overlap happens on the "right side" of the node...
        if (left_node_id == nid_t(node_id)) {
            auto length = get_overlap_length(edge, 0);

            if (not gfa_graph.get_is_reverse(edge.first)) {
                // Strictly adding entries to the map, so [] is ok here
                factored_overlaps[1][index.biclique_index].emplace_back(index.edge_index, length);
            }
            else {
                // Strictly adding entries to the map, so [] is ok here
                factored_overlaps[0][index.biclique_index].emplace_back(index.edge_index, length);
            }
        }
        if (right_node_id == nid_t(node_id)) {
            auto length = get_overlap_length(edge, 1);

            if (not gfa_graph.get_is_reverse(edge.second)) {
                // Strictly adding entries to the map, so [] is ok here
                factored_overlaps[0][index.biclique_index].emplace_back(index.edge_index, length);
            }
            else{
                // Strictly adding entries to the map, so [] is ok here
                factored_overlaps[1][index.biclique_index].emplace_back(index.edge_index, length);
            }
        }

        if (left_node_id != nid_t(node_id) and right_node_id != nid_t(node_id)){
            throw runtime_error("ERROR: parent node not found on either side of edge.\n"
                                "\tParent: " + to_string(node_id) + '\n' +
                                "\tEdge: " + to_string(gfa_graph.get_id(edge.first)) +
                                "->" + to_string(gfa_graph.get_id(edge.second)));
        }
    }
}


// For one node, make a mapping: (side -> (biclique_index -> (edge_index,length) ) )
void NodeInfo::factor_overlaps_by_biclique_and_side() {

    for (auto& index: node_to_biclique_edge[node_id]) {
        auto& edge = bicliques[index];

        auto left_node_id = gfa_graph.get_id(edge.first);
        auto right_node_id = gfa_graph.get_id(edge.second);

        // If the node is on the "left" of an edge then the overlap happens on the "right side" of the node...
        if (left_node_id == nid_t(node_id)) {
            auto length = get_overlap_length(edge, 0);

            if (not gfa_graph.get_is_reverse(edge.first)) {
                // Strictly adding entries to the map, so [] is ok here
                factored_overlaps[1][index.biclique_index].emplace_back(index.edge_index, length);
            }
            else {
                // Strictly adding entries to the map, so [] is ok here
                factored_overlaps[0][index.biclique_index].emplace_back(index.edge_index, length);
            }
        }
        if (right_node_id == nid_t(node_id)) {
            auto length = get_overlap_length(edge, 1);

            if (not gfa_graph.get_is_reverse(edge.second)) {
                // Strictly adding entries to the map, so [] is ok here
                factored_overlaps[0][index.biclique_index].emplace_back(index.edge_index, length);
            }
            else{
                // Strictly adding entries to the map, so [] is ok here
                factored_overlaps[1][index.biclique_index].emplace_back(index.edge_index, length);
            }
        }
    }
}


void NodeInfo::sort_factored_overlaps() {
    // First sort each biclique by its constituent edges
    for (bool side: {0, 1}) {
        auto& biclique_edge_indexes = factored_overlaps[side];

        for (auto& biclique: biclique_edge_indexes) {
            auto& biclique_index = biclique.first;
            auto& overlap_infos = biclique.second;

            sort(overlap_infos.begin(), overlap_infos.end(), [&](OverlapInfo a, OverlapInfo b) {
                return a.length > b.length;
            });
        }
    }
}


void NodeInfo::get_sorted_biclique_extents(
        array<deque<size_t>, 2>& sorted_extents_per_side,
        array<deque<size_t>, 2>& sorted_bicliques_per_side) const{

    sorted_extents_per_side = {};
    sorted_bicliques_per_side = {};

    for (auto side: {0, 1}) {
        vector<pair<size_t, size_t> > sorted_biclique_extents;

        const auto& biclique_edge_indexes = factored_overlaps[side];

        // Collect all the longest overlaps for each biclique (NodeInfo keeps them in descending sorted order)
        for (const auto& biclique: biclique_edge_indexes) {
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
        for (auto& item: sorted_biclique_extents) {
            sorted_bicliques_per_side[side].emplace_back(item.first);
            sorted_extents_per_side[side].emplace_back(item.second);
        }
    }
}

}