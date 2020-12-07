#include "handle_to_gfa.hpp"


namespace bluntifier {


char get_reversal_character(const HandleGraph& graph, const handle_t& node){
    bool reversed = graph.get_is_reverse(node);

    if (reversed){
        return '-';
    }
    else{
        return '+';
    }
}


void write_node_to_gfa(const HandleGraph& graph, const handle_t& node, ofstream& output_file){
    output_file << "S\t" << graph.get_id(node) << '\t' << graph.get_sequence(node) << '\n';
}


void write_edge_to_gfa(const HandleGraph& graph, const edge_t& edge, ofstream& output_file){
    output_file << "L\t" << graph.get_id(edge.first) << '\t' << get_reversal_character(graph, edge.first) << '\t'
                << graph.get_id(edge.second) << '\t' << get_reversal_character(graph, edge.second) << '\t'
                << "0M" << '\n';
}


/// With no consideration for directionality, just dump all the edges/nodes into GFA format
void handle_graph_to_gfa(const HandleGraph& graph, const string& output_path){
    std::cerr << "Writing GFA to file: " << output_path << '\n';

    ofstream output_gfa(output_path);

    if (not output_gfa.good()){
        throw runtime_error("ERROR: output file could not be written: " + output_path);
    }

    output_gfa << "H\tHVN:Z:1.0\n";

    graph.for_each_handle([&](const handle_t& node){
        write_node_to_gfa(graph, node, output_gfa);
    });

    graph.for_each_edge([&](const edge_t& edge){
        write_edge_to_gfa(graph, edge, output_gfa);
    });
}


// TODO write this method to use the overlaps and id map to write the linkages/sequences in the canonical direction
// using the canonical names as well, wherever possible
void handle_graph_to_canonical_gfa(const HandleGraph& graph, const string& output_path){
    ofstream output_gfa(output_path);

//    graph.for_each_handle([&](handle_t& node){
//
//    });
//
//    graph.for_each_edge([&](edge_t& edge){
//
//    });
}



}
