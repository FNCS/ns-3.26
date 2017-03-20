/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * WAN PMU model for TDC
 *
 */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("PMU_WAN");

int
main (int argc, char *argv[])
{
  CommandLine cmd;
  cmd.Parse (argc, argv);
  
  // LogComponentEnable ("UdpEchoClientApplication", LOG_LEVEL_INFO);
  // LogComponentEnable ("UdpEchoServerApplication", LOG_LEVEL_INFO);

  NS_LOG_INFO("Creating and naming nodes...");
  NodeContainer nodes;
  nodes.Create (2);
  Names::Add ("receiving", nodes[0]);
  Names::Add ("sending", nodes[1]);
  
  NS_LOG_INFO("Setting up channel...");
  PointToPointHelper pointToPoint;
  pointToPoint.SetDeviceAttribute ("DataRate", StringValue ("5Mbps"));
  pointToPoint.SetChannelAttribute ("Delay", StringValue ("2ms"));

  NS_LOG_INFO("Creating net devices...");
  NetDeviceContainer devices;
  devices = pointToPoint.Install (nodes);

  NS_LOG_INFO("Installing protocol stack and assigning IPs...");
  InternetStackHelper stack;
  stack.Install (nodes);
  Ipv4AddressHelper address;
  address.SetBase ("10.1.1.0", "255.255.255.0");
  Ipv4InterfaceContainer interfaces = address.Assign (devices);

  NS_LOG_INFO("Installing congesting traffic applications");
  uint16_t port = 50000;
  Address sinkAddress (InetSocketAddress (Ipv4Address::GetAny (), port));
  PacketSinkHelper packetSinkHelper ("ns3::TcpSocketFactory", sinkAddress);
  ApplicationContainer receiveApp = packetSinkHelper.Install (Names::Find<Node>(std::string("/Names/receiving")));
  receiveApp.Start (Seconds (1.0));
  receiveApp.Stop (Seconds (10.0));
  
  
  std::ostringstream onTime, offTime;
  onTime << "ns3::UniformRandomVariable[Min=" << onMin << "|Max=" << onMax << "]";
  offTime << "ns3::UniformRandomVariable[Min=" << offMin << "|Max=" << offMax << "]";
  OnOffHelper onOffHelper ("ns3::TcpSocketFactory", address[0]);
  onOffHelper.SetAttribute ("OnTime", StringValue ("ns3::UniformRandomVariable[Min=0.|Max=1.]"));
  onOffHelper.SetAttribute ("OffTime", StringValue ("ns3::UniformRandomVariable[Min=0.|Max=1.]"));
  onOffHelper.SetAttribute ("DataRate", );
  onOffHelper.SetAttribute ("PacketSize", );

  Simulator::Run ();
  Simulator::Destroy ();
  return 0;
}
