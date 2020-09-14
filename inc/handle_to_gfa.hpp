#ifndef BLUNTIFIER_HANDLE_TO_GFA_HPP
#define BLUNTIFIER_HANDLE_TO_GFA_HPP

#include "handlegraph/handle_graph.hpp"
#include <fstream>

using handlegraph::HandleGraph;
using handlegraph::handle_t;
using handlegraph::edge_t;
using std::runtime_error;
using std::ofstream;
using std::string;


namespace bluntifier {


char get_reversal_character(const HandleGraph& graph, const handle_t& node);


void write_node_to_gfa(const HandleGraph& graph, const handle_t& node, ofstream& output_file);


void write_edge_to_gfa(const HandleGraph& graph, const edge_t& edge, ofstream& output_file);


/// With no consideration for directionality, just dump all the edges/nodes into GFA format
void handle_graph_to_gfa(const HandleGraph& graph, const string& output_path);


// TODO write this method to use the overlaps and id map to write the linkages/sequences in the canonical direction
// using the canonical names as well, wherever possible
void handle_graph_to_canonical_gfa(const HandleGraph& graph, const string& output_path);

}

#endif //BLUNTIFIER_HANDLE_TO_GFA_HPP
