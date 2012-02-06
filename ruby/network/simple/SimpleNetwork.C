
/*
    Copyright (C) 1999-2008 by Mark D. Hill and David A. Wood for the
    Wisconsin Multifacet Project.  Contact: gems@cs.wisc.edu
    http://www.cs.wisc.edu/gems/

    --------------------------------------------------------------------

    This file is part of the Ruby Multiprocessor Memory System Simulator, 
    a component of the Multifacet GEMS (General Execution-driven 
    Multiprocessor Simulator) software toolset originally developed at 
    the University of Wisconsin-Madison.

    Ruby was originally developed primarily by Milo Martin and Daniel
    Sorin with contributions from Ross Dickson, Carl Mauer, and Manoj
    Plakal.

    Substantial further development of Multifacet GEMS at the
    University of Wisconsin was performed by Alaa Alameldeen, Brad
    Beckmann, Jayaram Bobba, Ross Dickson, Dan Gibson, Pacia Harper,
    Derek Hower, Milo Martin, Michael Marty, Carl Mauer, Michelle Moravan,
    Kevin Moore, Andrew Phelps, Manoj Plakal, Daniel Sorin, Haris Volos, 
    Min Xu, and Luke Yen.
    --------------------------------------------------------------------

    If your use of this software contributes to a published paper, we
    request that you (1) cite our summary paper that appears on our
    website (http://www.cs.wisc.edu/gems/) and (2) e-mail a citation
    for your published paper to gems@cs.wisc.edu.

    If you redistribute derivatives of this software, we request that
    you notify us and either (1) ask people to register with us at our
    website (http://www.cs.wisc.edu/gems/) or (2) collect registration
    information and periodically send it to us.

    --------------------------------------------------------------------

    Multifacet GEMS is free software; you can redistribute it and/or
    modify it under the terms of version 2 of the GNU General Public
    License as published by the Free Software Foundation.

    Multifacet GEMS is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with the Multifacet GEMS; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
    02111-1307, USA

    The GNU General Public License is contained in the file LICENSE.

### END HEADER ###
*/

/*
 * SimpleNetwork.C
 *
 * Description: See SimpleNetwork.h
 *
 * $Id$
 *
 */

#include "SimpleNetwork.h"
#include "Profiler.h"
#include "System.h"
#include "Switch.h"
#include "NetDest.h"
#include "Topology.h"
#include "TopologyType.h"
#include "MachineType.h"
#include "MessageBuffer.h"
#include "Protocol.h"
#include "Map.h"

//First try. Integrating topaz inside ruby in a static way
// Required includes
#ifdef USE_TOPAZ
#include <TPZSimulator.hpp> //Main simulator include
#include <math.h>
#include <fstream>
using namespace std;
#define TOPAZ_INI_FILE   "./rubsic.ini" //Name of configuration file.
#define TOPAZ_OWN_FILE   "./TPZSimul.ini"
#define FLITSIZE     4
#define COMMANDSIZE  8
#endif

// ***BIG HACK*** - This is actually code that _should_ be in Network.C

// Note: Moved to Princeton Network
// calls new to abstract away from the network
/*
Network* Network::createNetwork(int nodes)
{
  return new SimpleNetwork(nodes);
}
*/

SimpleNetwork::SimpleNetwork(int nodes)
{
  m_nodes = MachineType_base_number(MachineType_NUM);

  m_virtual_networks = NUMBER_OF_VIRTUAL_NETWORKS;
  m_endpoint_switches.setSize(m_nodes);

  m_in_use.setSize(m_virtual_networks);
  m_ordered.setSize(m_virtual_networks);
  for (int i = 0; i < m_virtual_networks; i++) {
    m_in_use[i] = false;
    m_ordered[i] = false;
  }
#ifdef USE_TOPAZ
	m_number_messages=0;
	m_number_ordered_messages=0;
	m_number_topaz_ordered_messages=0;
	m_number_topaz_messages=0;
	m_totalNetMsg=0;
	m_totalTopazMsg=0;
	m_forward_mapping = new SwitchID[m_nodes];
	m_reverse_mapping.setSize(m_nodes);
#endif
	
  // Allocate to and from queues
  m_toNetQueues.setSize(m_nodes);
  m_fromNetQueues.setSize(m_nodes);
  for (int node = 0; node < m_nodes; node++) {
    m_toNetQueues[node].setSize(m_virtual_networks);
    m_fromNetQueues[node].setSize(m_virtual_networks);
    for (int j = 0; j < m_virtual_networks; j++) {
      m_toNetQueues[node][j] = new MessageBuffer;
#ifdef USE_TOPAZ
		m_toNetQueues[node][j]->setInPort();
		m_toNetQueues[node][j]->setNetwork(this);
		m_toNetQueues[node][j]->setVNet(j);
#endif
		m_fromNetQueues[node][j] = new MessageBuffer;
#ifdef USE_TOPAZ
		m_fromNetQueues[node][j]->setOutPort();
		m_fromNetQueues[node][j]->setNetwork(this);
		m_fromNetQueues[node][j]->setVNet(j);
#endif
    }
  }

  // Setup the network switches
  m_topology_ptr = new Topology(this, m_nodes);
  int number_of_switches = m_topology_ptr->numSwitches();
  for (int i=0; i<number_of_switches; i++) {
    m_switch_ptr_vector.insertAtBottom(new Switch(i, this));
  }
  m_topology_ptr->createLinks(false);  // false because this isn't a reconfiguration
	//
	//  TOPAZ INITIALIZATION AND INSTALLERS
	//
	//
#ifdef USE_TOPAZ
	//Support for CMP or SMP but not SMP of CMP
	if (g_PROCS_PER_CHIP > 1 && g_NUM_PROCESSORS > g_PROCS_PER_CHIP) {
		cerr<<endl<<"<TOPAZ> Feature not implemented. Only CMP or non-CMP SMPs </TOPAZ>"<<endl;
		exit(-1);
	}
	
	//A crossbar network in this context means a ideal network
	if (TopologyType_CROSSBAR == string_to_TopologyType(g_NETWORK_TOPOLOGY)) {
		cout<<endl<<"<TOPAZ> Topaz will be disabled we run only with a ideal Network </TOPAZ>"<<endl;
		cout<<endl<<"<TOPAZ> If this is not intended, you should UNSET Crossbar as top. </TOPAZ>"<<endl;
		m_permanentDisable=true;
		return;
	}
	
	m_permanentDisable=false;
	
	//TOPAZ initialization file is here?
	//(otherwise segment.fault)
	ifstream own(TOPAZ_OWN_FILE);
	if (! own.good()) {
		cerr << "<TOPAZ>  Can't open topaz init file :: " << TOPAZ_OWN_FILE << endl;
		cerr<<" That file must have the route to Router, Network & Simulation SGML"<< endl;
		cerr<<"</TOPAZ>"<<endl;
		exit(-1);
	}
	own.close();
	//from rubisic.ini
	TPZString initString ;
	if (TPZString(g_TOPAZ_SIMULATION) == TPZString("FILE")) {
		ifstream ifs(TOPAZ_INI_FILE);
		if (! ifs.good()) {
			cerr << "<TOPAZ>  Can't open topaz-ruby conf file :: " << TOPAZ_INI_FILE << endl;
			cerr<<" That file must include a line which refers to the network you want to use "<< endl;
			cerr<<" (or define g_TOPAZ_SIMULATION with the name of simulation desired)</TOPAZ>"<<endl;
			exit(-1);
		}
		initString = TPZString::lineFrom(ifs);
		ifs.close();
	}
	else {
		if (g_NUM_TOPAZ_THREADS>0) {
		  initString  = TPZString("TPZSimul -N ") + TPZString(g_NUM_TOPAZ_THREADS) + TPZString(" -q -s  ")+ TPZString(g_TOPAZ_SIMULATION);
		} 
		else {
		  initString  = TPZString("TPZSimul -q -s  ")+ TPZString(g_TOPAZ_SIMULATION);
		}
	}
	
	initString = initString + TPZString(" -t EMPTY -d 1000");     
#ifdef TOPAZ_TRACE
  initString = initString + TPZString(" -m ") + TPZString(g_TOPAZ_TRACE_FILE);
  initString = initString + TPZString(",1000000");     
#endif
	
	m_firstTrigger=~0;
	//initString   += TPZString(" -L ") + TPZString(m_dataSizeTopaz)+TPZString(" -u ")+TPZString(4*m_dataSizeTopaz)+TPZString(" -r ");    // L de los buffers para CT
	for (int i = 0; i < m_virtual_networks; i++) {
		TPZSIMULATOR()->createSimulation(initString); 
		//Simulated networks go from 1,2... (Watch out! is +1 than slicc's vnets) 
		if (TPZSIMULATOR()->getSimulation(1)->needToUnify()) {
			m_unify=1;
			break;
		}
		else {
			m_unify=m_virtual_networks; 
		}
	} 
	
	if (!g_TOPAZ_FLIT_SIZE) { //Not specified by ruby conf
		//Adjust buffers & Msg SIZE form SGML specification
		m_flitSize=TPZSIMULATOR()->getSimulation(1)->getFlitSize();
		cerr<<"Warning:g_TOPAZ_FLIT_SIZE was not specified in rubyconfig.defaults. It will be get from SGML specification."<<endl;
	}
	else {
		m_flitSize=g_TOPAZ_FLIT_SIZE;
	}
	
	if (m_flitSize == 0) {
		cerr<<"<TOPAZ> FLITSIZE must be specified in simulation sgml file as <LinkWidth id=[whatever bytes]" << endl;
		cerr<<"Closing simulation </TOPAZ>" << endl;
		exit(-1);
	}
	
	m_commandSizeTopaz=g_TOPAZ_COMMAND_SIZE/m_flitSize;
	m_dataSizeTopaz=(g_DATA_BLOCK_BYTES+g_TOPAZ_COMMAND_SIZE)/m_flitSize;
	
	for (unsigned i = 1; i <= m_unify; i++) {
		TPZSIMULATOR()->getSimulation(i)->setPacketLength(m_dataSizeTopaz); //VCT applied. Largest packet 
	}
	
	//Clock ratio entre simuladores
	if (g_TOPAZ_CLOCK_RATIO==0) {
		m_processorClockRatio=int(TPZSIMULATOR()->getSimulation(1)->getNetworkClockRatioSGML());
	}
	else {
		m_processorClockRatio=g_TOPAZ_CLOCK_RATIO;
	}
	assert(m_processorClockRatio>=1);
	
	this->enableTopaz(); //If some one need to run with topaz (recorder ... vgr... should disable
	srandom(g_RANDOM_SEED); //Topaz may try to adjust random seed in initialization. Do it again for variab
	srand(g_RANDOM_SEED);
	srand48(g_RANDOM_SEED);
#endif
}

#ifdef USE_TOPAZ
int SimpleNetwork::getMessageSizeTopaz(MessageSizeType size_type) const {
	switch(size_type) {
		case MessageSizeType_Control:
		case MessageSizeType_Request_Control:
		case MessageSizeType_Reissue_Control:
		case MessageSizeType_Response_Control:
		case MessageSizeType_Writeback_Control:
		case MessageSizeType_Forwarded_Control:
		case MessageSizeType_Invalidate_Control:
		case MessageSizeType_Unblock_Control:
		case MessageSizeType_Persistent_Control:
		case MessageSizeType_Completion_Control:
			return m_commandSizeTopaz;
			break;
		case MessageSizeType_Data:
		case MessageSizeType_Response_Data:
		case MessageSizeType_ResponseLocal_Data:
		case MessageSizeType_ResponseL2hit_Data:
		case MessageSizeType_Writeback_Data:
			return m_dataSizeTopaz;
			break;
		default:
			ERROR_MSG("Invalid range for type MessageSizeType");
			break;
	}
	return 0;
}

int SimpleNetwork::isRequest(MessageSizeType size_type) const {
	switch(size_type) {
		case MessageSizeType_Control:
		case MessageSizeType_Request_Control:
		case MessageSizeType_Reissue_Control:
		case MessageSizeType_Response_Control:
		case MessageSizeType_Writeback_Control:
		case MessageSizeType_Forwarded_Control:
		case MessageSizeType_Unblock_Control:
		case MessageSizeType_Persistent_Control:
			return 1;
			break;
		case MessageSizeType_Data:
		case MessageSizeType_Response_Data:
		case MessageSizeType_ResponseLocal_Data:
		case MessageSizeType_ResponseL2hit_Data:
		case MessageSizeType_Writeback_Data:
		case MessageSizeType_Invalidate_Control:
		case MessageSizeType_Completion_Control:
			return 0;
			break;
        default:
			ERROR_MSG("Invalid range for type MessageSizeType");
			break;
	}
	return 0;
}  

bool SimpleNetwork::useGemsNetwork(int vnet) {
	
	// During warmup we use GEMS' network
	if (unlikely(inWarmup()))
		return true;
	
	// Messages in order must go all through the same network. If this is an
	// ordered virtual network, send the message through the network where you
	// find any message.
	if (unlikely(isVNetOrdered(vnet))) {
	   if((g_NUM_TOPAZ_THREADS>0) && (g_MAX_MESSAGES_THROUGH_GEMS>0)) {
	      assert(numberOfTopazOrderedMessages() == 0);
	      return true;
	   } 
		 else {
		 	if (numberOfOrderedMessages() > 0) {
				assert(numberOfTopazOrderedMessages() == 0);
				return true;
			} 
			else if (numberOfTopazOrderedMessages() > 0) {
				return false;
			} 
    }
	}
	
	const int messages_in_ruby = numberOfMessages();
	// FIXME: Topaz doesn't count correctly the number of messages. When this
	// happens, we route all messages through topaz to be on the safe side.
	// If you want to use g_MAX_MESSAGES_THROUGH_GEMS,
	// fix TPZSIMULATOR()->getSimulation(1)->getNetwork()->getMessagesInNet()
	if (m_number_topaz_messages < 0) {
		static bool warn_topaz_count_is_wrong = true;
		if (unlikely(warn_topaz_count_is_wrong)) {
			WARN_EXPR(m_number_topaz_messages);
			WARN_MSG("Topaz is not counting messages correctly, ignoring g_MAX_MESSAGES_THROUGH_GEMS.");
			WARN_MSG("Please, fix topaz if you want to use this parameter.");
			warn_topaz_count_is_wrong = false;
		}
		return false;
	}
	
	// If the total number of messages is small (less than g_MAX_MESSAGES_THROUGH_GEMS), we can use the simplistic GEMS network
	if ((messages_in_ruby + m_number_topaz_messages) < g_MAX_MESSAGES_THROUGH_GEMS)
		return true;
	else
		return false;
}
#endif //USE_TOPAZ

void SimpleNetwork::reset()
{
  for (int node = 0; node < m_nodes; node++) {
    for (int j = 0; j < m_virtual_networks; j++) {
      m_toNetQueues[node][j]->clear();
      m_fromNetQueues[node][j]->clear();
    }
  }

  for(int i=0; i<m_switch_ptr_vector.size(); i++){
    m_switch_ptr_vector[i]->clearBuffers();
  }
}

SimpleNetwork::~SimpleNetwork()
{
  for (int i = 0; i < m_nodes; i++) {
    m_toNetQueues[i].deletePointers();
    m_fromNetQueues[i].deletePointers();
  }
  m_switch_ptr_vector.deletePointers();
  m_buffers_to_free.deletePointers();
  delete m_topology_ptr;
}

// From a switch to an endpoint node
void SimpleNetwork::makeOutLink(SwitchID src, NodeID dest, const NetDest& routing_table_entry, int link_latency, int link_weight, int bw_multiplier, bool isReconfiguration)
{
  assert(dest < m_nodes);
  assert(src < m_switch_ptr_vector.size());
  assert(m_switch_ptr_vector[src] != NULL);
  if(!isReconfiguration){
    m_switch_ptr_vector[src]->addOutPort(m_fromNetQueues[dest], routing_table_entry, link_latency, bw_multiplier);
    m_endpoint_switches[dest] = m_switch_ptr_vector[src];
  } else {
    m_switch_ptr_vector[src]->reconfigureOutPort(routing_table_entry);
  }
}

// From an endpoint node to a switch
void SimpleNetwork::makeInLink(NodeID src, SwitchID dest, const NetDest& routing_table_entry, int link_latency, int bw_multiplier, bool isReconfiguration)
{
  assert(src < m_nodes);
  if(!isReconfiguration){
    m_switch_ptr_vector[dest]->addInPort(m_toNetQueues[src]);
  } else {
    // do nothing
  }
}

// From a switch to a switch
void SimpleNetwork::makeInternalLink(SwitchID src, SwitchID dest, const NetDest& routing_table_entry, int link_latency, int link_weight, int bw_multiplier, bool isReconfiguration)
{
  if(!isReconfiguration){
    // Create a set of new MessageBuffers
    Vector<MessageBuffer*> queues;
    for (int i = 0; i < m_virtual_networks; i++) {
      // allocate a buffer
      MessageBuffer* buffer_ptr = new MessageBuffer;
      buffer_ptr->setOrdering(true);
      if(FINITE_BUFFERING) {
        buffer_ptr->setSize(FINITE_BUFFER_SIZE); 
      }
      queues.insertAtBottom(buffer_ptr);
      // remember to deallocate it
      m_buffers_to_free.insertAtBottom(buffer_ptr);
    }
  
    // Connect it to the two switches
    m_switch_ptr_vector[dest]->addInPort(queues);
    m_switch_ptr_vector[src]->addOutPort(queues, routing_table_entry, link_latency, bw_multiplier);
  } else {
    m_switch_ptr_vector[src]->reconfigureOutPort(routing_table_entry);
  }
}

void SimpleNetwork::checkNetworkAllocation(NodeID id, bool ordered, int network_num)
{
  ASSERT(id < m_nodes);
  ASSERT(network_num < m_virtual_networks);

  if (ordered) {
    m_ordered[network_num] = true;
  }
  m_in_use[network_num] = true;
}

MessageBuffer* SimpleNetwork::getToNetQueue(NodeID id, bool ordered, int network_num)
{
  checkNetworkAllocation(id, ordered, network_num);
  return m_toNetQueues[id][network_num];
}

MessageBuffer* SimpleNetwork::getFromNetQueue(NodeID id, bool ordered, int network_num)
{
  checkNetworkAllocation(id, ordered, network_num);
  return m_fromNetQueues[id][network_num];
}

const Vector<Throttle*>* SimpleNetwork::getThrottles(NodeID id) const
{
  assert(id >= 0);
  assert(id < m_nodes);
  assert(m_endpoint_switches[id] != NULL);
  return m_endpoint_switches[id]->getThrottles();
}

void SimpleNetwork::printStats(ostream& out) const
{
#ifdef USE_TOPAZ
  out<<endl;
	out<<endl;
	out<<" -----------------------------------------------"<< endl;
	out<<"| <TOPAZ>: network performance and configuration|"<<endl;
	out<<" -----------------------------------------------"<<endl;
	out<<endl;
  out<<"Ratio Processor Clock/Network Clock = "<<m_processorClockRatio<<endl;
  out<<"Flit size in bytes                  = "<<m_flitSize<<" bytes"<<endl;
  out<<"Command packets size                = "<<m_commandSizeTopaz<<" flits"<<endl;
  out<<"Data packet size                    = "<<m_dataSizeTopaz<<" flits"<<endl;
  for(unsigned currentVnet=1;currentVnet<=m_unify;currentVnet++) { 
	
    TPZSIMULATOR()->getSimulation(currentVnet)->writeSimulationStatus(out);
    TPZSIMULATOR()->getSimulation(currentVnet)->getNetwork()->writeComponentStatus(out);
  }
  out<<"</TOPAZ>"<<endl;
#else
  out << endl;
  out << "Network Stats" << endl;
  out << "-------------" << endl;
  out << endl;
  for(int i=0; i<m_switch_ptr_vector.size(); i++) {
    m_switch_ptr_vector[i]->printStats(out);
  }
#endif
}

void SimpleNetwork::clearStats()
{
  for(int i=0; i<m_switch_ptr_vector.size(); i++) {
    m_switch_ptr_vector[i]->clearStats();
  }
}

void SimpleNetwork::printConfig(ostream& out) const 
{
  out << endl;
  out << "Network Configuration" << endl;
  out << "---------------------" << endl;
  out << "network: SIMPLE_NETWORK" << endl;
  out << "topology: " << g_NETWORK_TOPOLOGY << endl;
  out << endl;

  for (int i = 0; i < m_virtual_networks; i++) {
    out << "virtual_net_" << i << ": ";
    if (m_in_use[i]) {
      out << "active, ";
      if (m_ordered[i]) {
        out << "ordered" << endl;
      } else {
        out << "unordered" << endl;
      }
    } else {
      out << "inactive" << endl;
    }
  }
  out << endl;
  for(int i=0; i<m_switch_ptr_vector.size(); i++) {
    m_switch_ptr_vector[i]->printConfig(out);
  }

  if (g_PRINT_TOPOLOGY) {
    m_topology_ptr->printConfig(out);
  }
}

void SimpleNetwork::print(ostream& out) const
{
  out << "[SimpleNetwork]";
}

#ifdef USE_TOPAZ
void SimpleNetwork::increaseNumMsg(int num)
{
	m_number_messages+=num;
	//m_totalNetMsg+=num;
}

void SimpleNetwork::increaseNumTopazMsg(int num)
{
	m_number_topaz_messages+=num;
	m_totalTopazMsg+=num;
}

void SimpleNetwork::decreaseNumMsg(int vnet)
{
  m_number_messages--;
	assert(m_number_messages >= 0);
	if (m_ordered[vnet]) {
	  if ( numberOfOrderedMessages() > 0 ) {
			assert(!(numberOfTopazOrderedMessages()>0));
			m_number_ordered_messages--;
		}
	}
}

void SimpleNetwork::decreaseNumTopazMsg (int vnet)
{
  m_number_topaz_messages--;
  assert(m_number_messages >= 0);
	if (m_ordered[vnet]) {
	  assert(!(numberOfOrderedMessages()>0));
		m_number_topaz_ordered_messages--;
	}
}

void SimpleNetwork::increaseNumOrderedMsg(int num)
{
	m_number_ordered_messages+=num;
}

void SimpleNetwork::increaseNumTopazOrderedMsg(int num)
{
	m_number_topaz_ordered_messages+=num;
}

/* More information about m_nodes at Topology.C
 *  0              \
 *  |              |  inputs
 *  |              /
 *  m_nodes-1
 *  m_nodes        \
 *  |              |  outputs
 *  |              /
 *  2*m_nodes-1
 *  2*m_nodes      \
 *  |              |  internal_nodes
 *  |              /
 *  3*m_nodes-1
 *
 * Parameter int_node is in the range of internal_nodes. In order to convert
 * it to the same number asseen in the network file, 2*m_nodes must be
 * substracted.
 */
void SimpleNetwork::setTopazMapping (SwitchID ext_node, SwitchID int_node) {
	int_node -= 2*m_nodes;
	MachineID machine = nodeNumber_to_MachineID(ext_node);
	
	m_forward_mapping[ext_node] = int_node;
	m_reverse_mapping[int_node].add(machine);
}

#endif
