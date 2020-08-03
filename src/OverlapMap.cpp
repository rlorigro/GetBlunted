#include "OverlapMap.hpp"

using std::make_pair;
using std::to_string;


namespace bluntifier{

OverlapMap::OverlapMap()=default;


void OverlapMap::insert(const gfak::edge_elem& e, handle_t source, handle_t sink){
    Alignment cigar(e.alignment);
    overlaps.insert({make_pair(source, sink), cigar});
}


void OverlapMap::insert(const gfak::edge_elem& e, const edge_t& edge_handle){
    Alignment cigar(e.alignment);
    overlaps.insert({edge_handle, cigar});
}


unordered_map<edge_t,Alignment>::iterator OverlapMap::at(handle_t source, handle_t sink){
    return overlaps.find(make_pair(source, sink));
}


unordered_map<edge_t,Alignment>::iterator OverlapMap::at(const edge_t& edge_handle){
    return overlaps.find(edge_handle);
}


unordered_map<edge_t,Alignment>::iterator OverlapMap::canonicalize_and_find(edge_t& edge, const HandleGraph& graph){
    // Check if edge is found in the overlaps in its provided orientation
    auto iter = overlaps.find(edge);
    if (iter == overlaps.end()){
        // If not found, then flip it and search again
        handle_t right = graph.flip(edge.first);
        handle_t left = graph.flip(edge.second);
        edge.first = left;
        edge.second = right;

        iter = overlaps.find(edge);

        // If it still isn't found then throw an error
        if (iter == overlaps.end()) {
            throw runtime_error("ERROR: edge not found in overlaps:\n\t("
                                + to_string(graph.get_id(edge.first)) + '-'
                                + to_string(graph.get_is_reverse(edge.first)) + ")->("
                                + to_string(graph.get_id(edge.second)) + '-'
                                + to_string(graph.get_is_reverse(edge.second)) + ')');
        }
    }

    return iter;
}


void OverlapMap::canonicalize_and_compute_lengths(pair<uint64_t,uint64_t>& lengths, edge_t& edge, const HandleGraph& graph){
    auto iter = canonicalize_and_find(edge, graph);
    iter->second.compute_lengths(lengths);
}


//void OverlapMap::convert_to_explicit_mismatch(Alignment& alignment, edge_t& edge, const HandleGraph& graph){
//    auto iter = canonicalize_and_find(edge, graph);
//
//    pair<uint64_t,uint64_t> lengths;
//    iter->second.compute_lengths(lengths);
//
//
//}



}