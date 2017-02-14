/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

// Test program for this 3-router scenario, using static routing
//
// (a.a.a.a/32)A<--x.x.x.0/30-->B<--y.y.y.0/30-->C(c.c.c.c/32)

#include <iostream>
#include <fstream>
#include <string>
#include <cassert>

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/csma-module.h"
#include "ns3/applications-module.h"
#include "ns3/ipv4-static-routing-helper.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("StaticRoutingSlash32Test");

int 
main (int argc, char *argv[])
{
    LogComponentEnable("OnOffApplication", LOG_LEVEL_INFO);
    LogComponentEnable("Ipv4StaticRouting", LOG_LEVEL_ALL);

  // Allow the user to override any of the defaults and the above
  // DefaultValue::Bind ()s at run-time, via command-line arguments
  CommandLine cmd;
  cmd.Parse (argc, argv);

  Ptr<Node> nCC = CreateObject<Node> ();
  Ptr<Node> nSS = CreateObject<Node> ();
  Ptr<Node> nENB = CreateObject<Node> ();

  NodeContainer backhaulNodes = NodeContainer (nCC, nSS, nENB);

  InternetStackHelper internet;
  internet.Install (backhaulNodes);

  // Point-to-point links
  NodeContainer ncCC_SS = NodeContainer (nCC, nSS);
  NodeContainer ncSS_ENB = NodeContainer (nSS, nENB);

  // Defining channels
  PointToPointHelper p2p;
  p2p.SetDeviceAttribute ("DataRate", StringValue ("5Mbps"));
  p2p.SetChannelAttribute ("Delay", StringValue ("2ms"));
  NetDeviceContainer dcCC_SS = p2p.Install (ncCC_SS);
  NetDeviceContainer dcSS_ENB = p2p.Install (ncSS_ENB);;
  
  // Defining IP addresses.
  Ipv4AddressHelper ipv4;
  ipv4.SetBase ("10.1.1.0", "255.255.255.252");
  Ipv4InterfaceContainer ifcCC_SS = ipv4.Assign (dcCC_SS);

  ipv4.SetBase ("10.1.1.4", "255.255.255.252");
  Ipv4InterfaceContainer ifcSS_ENB = ipv4.Assign (dcSS_ENB);

  Ptr<Ipv4> ipv4CC = nCC->GetObject<Ipv4> ();
  Ptr<Ipv4> ipv4SS = nSS->GetObject<Ipv4> ();
  Ptr<Ipv4> ipv4ENB = nENB->GetObject<Ipv4> ();


 
  Ipv4StaticRoutingHelper ipv4RoutingHelper;
  // Create static routes from CC to ENB
  Ptr<Ipv4StaticRouting> staticRoutingCC = ipv4RoutingHelper.GetStaticRouting(ipv4CC);
  // The ifIndex for this downlink route is 1; the first p2p link added
  staticRoutingCC->AddHostRouteTo (Ipv4Address (ifcSS_ENB.GetAddress(1)), ifcCC_SS.GetAddress(1), 1);
  Ptr<Ipv4StaticRouting> staticRoutingSS = ipv4RoutingHelper.GetStaticRouting(ipv4SS);
  // The ifIndex we want on the eNodeB (ENB) is 2; 0 corresponds to loopback, and 1 to the first point to point link
  staticRoutingSS->AddHostRouteTo (Ipv4Address (ifcSS_ENB.GetAddress(1)), ifcSS_ENB.GetAddress(1), 2);
  // Create the OnOff application to send UDP datagrams of size
  // 210 bytes at a rate of 448 Kb/s
  uint16_t port = 9;   // Discard port (RFC 863)
  OnOffHelper onoff ("ns3::UdpSocketFactory", 
                     Address (InetSocketAddress (ifcSS_ENB.GetAddress(1), port)));
  onoff.SetConstantRate (DataRate (6000));
  ApplicationContainer apps = onoff.Install (nCC);
  apps.Start (Seconds (1.0));
  apps.Stop (Seconds (3.0));

  // Create a packet sink to receive these packets
  PacketSinkHelper sink ("ns3::UdpSocketFactory",
                         Address (InetSocketAddress (Ipv4Address::GetAny (), port)));
  apps = sink.Install (nENB);
  apps.Start (Seconds (1.0));
  apps.Stop (Seconds (5.0));

  AsciiTraceHelper ascii;
  p2p.EnableAsciiAll (ascii.CreateFileStream ("static-routing-slash32_test.tr"));
  p2p.EnablePcapAll ("static-routing-slash32");

    Ptr<OutputStreamWrapper> routingStream = Create<OutputStreamWrapper> ("static_routing_example_table.routes", std::ios::out);
    staticRoutingCC->PrintRoutingTable(routingStream);
    staticRoutingSS->PrintRoutingTable(routingStream);

  Simulator::Run ();
  Simulator::Destroy ();

  return 0;
}
