/*******************************************************************************
Model for RADICS NREWCA work. Uses LTE to get information from smart meter to
aggregator in substation and then a dedicated line from substation to the
control room. 

To support man-in-the-middle attacks, added a node between LTE PGW and 
substation aggregator as well as between substation aggregator and control 
center.


Trevor Hardy

*******************************************************************************/
#include <iterator>
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <cstdlib>
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/csma-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/ipv4-static-routing-helper.h"
#include "ns3/ipv4-nix-vector-helper.h"
#include "ns3/error-model.h"
#include "ns3/names.h"
#include "ns3/node-list.h"
#include "ns3/fncs-application.h"
#include "ns3/fncs-application-helper.h"
#include "ns3/fncs-simulator-impl.h"
#include <ns3/core-module.h>
#include <ns3/network-module.h>
#include <ns3/mobility-module.h>
#include <ns3/lte-module.h>


using namespace ns3;
NS_LOG_COMPONENT_DEFINE ("RADICS_SM");

// CSV reader stuff
class CSVRow
{
    public:
        std::string const& operator[](std::size_t index) const
        {
            return m_data[index];
        }
        std::size_t size() const
        {
            return m_data.size();
        }
        void readNextRow(std::istream& str)
        {
            std::string         line;
            std::getline(str,line);

            std::stringstream   lineStream(line);
            std::string         cell;

            m_data.clear();
            while(std::getline(lineStream,cell,','))
            {
                m_data.push_back(cell);
            }
        }
    private:
        std::vector<std::string>    m_data;
};

std::istream& operator>>(std::istream& str,CSVRow& data)
{
  data.readNextRow(str);
  return str;
}




int main (int argc, char *argv[]) {
    //Defining all my logging
    LogComponentEnable("RADICS_SM", LOG_LEVEL_ALL);
    //LogComponentEnable("LteUeRrc", LOG_LEVEL_INFO);
    //LogComponentEnable("LteEnbRrc", LOG_LEVEL_INFO);
    //LogComponentEnable("LteUeNetDevice", LOG_LEVEL_INFO);
    //LogComponentEnable("EpcSgwPgwApplication", LOG_LEVEL_INFO);
    //LogComponentEnable("EpcEnbApplication", LOG_LEVEL_INFO);
    //LogComponentEnable("Ipv4AddressHelper", LOG_LEVEL_ALL);
    LogComponentEnable("OnOffApplication", LOG_LEVEL_INFO);
    LogComponentEnable("Ipv4StaticRouting", LOG_LEVEL_INFO);
    //LogComponentEnable("PointToPointChannel", LOG_LEVEL_ALL);
    //LogComponentEnable("PointToPointNetDevice", LOG_LEVEL_ALL);
    //LogComponentEnable("PointToPointNetDevice", LOG_LEVEL_ALL);
    //LogComponentEnable("Ipv4InterfaceAddress", LOG_LEVEL_LOGIC);

    // Reading in list of nodes and their locations
    std::vector<std::string> nodeName;
    std::vector<double> nodeX;
    std::vector<double> nodeY;

    std::ifstream  topoFile("/Users/hard312/models/Multi-source sub (RADICS)/R4-12.47-1_sm_positions_m.csv");
    CSVRow row;
    while(topoFile >> row)
    {
        //NS_LOG_DEBUG("Parsing row: " << row[0] << "\t" << row[1] << "\t" << row[2]);
        double nodeX_double = (double)atof(row[1].c_str());
        double nodeY_double = (double)atof(row[2].c_str());
        
        // Finding substation location
        std::string substationNode = "R4-12-47-1_node_572";
        float subX = 0;
        float subY = 0;
        if (row[0].compare(substationNode) == 0){
            subX = nodeX_double;
            subY = nodeY_double;
            NS_LOG_DEBUG("Substation coordinates: " << subX << ", " << subY);
        }
        
        // Finding triplex meters (smart meters)
        std::string tm = row[0].substr(11,2); 
        if (tm.compare("tm") == 0) { // row is a triplex meter
            nodeName.push_back(row[0]);
            nodeX.push_back(nodeX_double);
            nodeY.push_back(nodeY_double);
            //NS_LOG_DEBUG("Smart meter node name: " << nodeName.back() << "\tNode X: " << nodeX.back() << "\tNode Y: " << nodeY.back());
        }
    }
    int numMeters = nodeName.size();
    NS_LOG_DEBUG("Number of triplex meters: " << numMeters);
    
    // Processing list for some specific information
    float minX = 0;
    float minY = 0;
    float maxX = 0;
    float maxY = 0;

    
    for (int idx=0; idx < numMeters; idx++){
        // Finding geographic center of feeder
        if (nodeX[idx] < minX){
            minX = nodeX[idx];
        }
        if (nodeX[idx] > maxX){
            maxX = nodeX[idx];
        }
        if (nodeY[idx] < minY){
            minY = nodeY[idx];
        }
        if (nodeY[idx] > maxY){
            maxY = nodeY[idx];
        }
        
    }
    float centerX = maxX-minX/2;
    float centerY = maxY-minY/2;
    NS_LOG_DEBUG("Geographic center of feeder: " << centerX << ", " << centerY);
    
    // Creating helpers
    Ptr<LteHelper> lteHelper = CreateObject<LteHelper>();
    Ptr<PointToPointEpcHelper> epcHelper = CreateObject<PointToPointEpcHelper>();
    lteHelper->SetEpcHelper(epcHelper);
    PointToPointHelper p2ph;
    Ipv4AddressHelper ipv4h;
    Ipv4StaticRoutingHelper ipv4RoutingHelper;

    //Creating nodes
    NodeContainer enbNodes, ueNodes, subStationNodes, mimNodes, controlCenterNodes;
    enbNodes.Create (1);
    subStationNodes.Create(1);
    mimNodes.Create(2);
    controlCenterNodes.Create(1);
    ueNodes.Create (numMeters);
    
    // Assume one P-GW  for all eNBs located outside of substation.
    Ptr<Node> pgw = epcHelper->GetPgwNode (); //Assigns IP 7.0.0.1 to P-GW node
    // Installing IP stack on all back-haul nodes
    InternetStackHelper internet;
    internet.Install (subStationNodes);
    internet.Install (mimNodes);
    internet.Install (controlCenterNodes);
 
    // Creating backhaul connections, all with the same characteristics (for now)
    p2ph.SetDeviceAttribute ("DataRate", DataRateValue (DataRate ("100Mb/s")));
    p2ph.SetDeviceAttribute ("Mtu", UintegerValue (1500));
    p2ph.SetChannelAttribute ("Delay", TimeValue (Seconds (0.010)));
    NetDeviceContainer control_mim1_devs  = p2ph.Install (controlCenterNodes.Get(0), mimNodes.Get(0));
    NetDeviceContainer mim1_substation_devs  = p2ph.Install (mimNodes.Get(0), subStationNodes.Get(0));
    NetDeviceContainer substation_mim2_devs  = p2ph.Install (subStationNodes.Get(0), mimNodes.Get(1));
    NetDeviceContainer mim2_pgw_devs = p2ph.Install (mimNodes.Get(1), pgw);
    
    // Assigning backhaul IPs
    ipv4h.SetBase ("10.0.0.0", "255.255.255.0", "0.0.0.1");
    Ipv4InterfaceContainer control_mim1_IpIfaces = ipv4h.Assign (control_mim1_devs);
    ipv4h.SetBase ("10.0.0.0", "255.255.255.0", "0.0.0.3");
    Ipv4InterfaceContainer mim1_substation_IpIfaces = ipv4h.Assign (mim1_substation_devs);
    ipv4h.SetBase ("10.0.0.0", "255.255.255.0", "0.0.0.5");
    Ipv4InterfaceContainer substation_mim2_IpIfaces = ipv4h.Assign (substation_mim2_devs);
    ipv4h.SetBase ("10.0.0.0", "255.255.255.0", "0.0.0.7");
    Ipv4InterfaceContainer mim2_pgw_IpIfaces = ipv4h.Assign (mim2_pgw_devs);
    Ipv4Address controlCenterAddr = control_mim1_IpIfaces.GetAddress(0);
    
    // Setting up static routing in the backhaul
    // Man-in-the-middle nodes will not be listed in routing tables but will correctly 
    //  echo packets on to their correct destination.
    Ptr<Ipv4> ipv4PGW = pgw->GetObject<Ipv4> ();
    Ptr<Ipv4> ipv4SS = subStationNodes.Get(0)->GetObject<Ipv4>();
    Ptr<Ipv4> ipv4CC = controlCenterNodes.Get(0)->GetObject<Ipv4>();
    Ptr<Ipv4> ipv4MIM1 = mimNodes.Get(0)->GetObject<Ipv4>();
    Ptr<Ipv4> ipv4MIM2 = mimNodes.Get(1)->GetObject<Ipv4>();
    Ptr<Ipv4StaticRouting> staticRoutingPGW = ipv4RoutingHelper.GetStaticRouting(ipv4PGW);
    Ptr<Ipv4StaticRouting> staticRoutingMIM2up = ipv4RoutingHelper.GetStaticRouting(ipv4MIM1);
    Ptr<Ipv4StaticRouting> staticRoutingSSup = ipv4RoutingHelper.GetStaticRouting(ipv4SS);
    Ptr<Ipv4StaticRouting> staticRoutingMIM1up = ipv4RoutingHelper.GetStaticRouting(ipv4MIM2);
    staticRoutingPGW->AddHostRouteTo(controlCenterAddr, mim2_pgw_IpIfaces.GetAddress(0), 1, 3);
    staticRoutingMIM2up->AddHostRouteTo(controlCenterAddr, substation_mim2_IpIfaces.GetAddress(0), 1, 2);
    staticRoutingSSup->AddHostRouteTo(controlCenterAddr, mim1_substation_IpIfaces.GetAddress(0), 1, 1);
    staticRoutingMIM1up->AddHostRouteTo(controlCenterAddr, controlCenterAddr, 1);
    
    // Adding applications to test flow from PGW to CC
    uint16_t port = 9;   // Discard port (RFC 863)
    OnOffHelper onoff ("ns3::UdpSocketFactory", 
                         Address (InetSocketAddress (controlCenterAddr, port)));
    onoff.SetConstantRate (DataRate (6000));
    ApplicationContainer apps = onoff.Install (pgw);
    apps.Start (Seconds (1.0));
    apps.Stop (Seconds (3.0));

    // Create a packet sink to receive these packets
    PacketSinkHelper sink ("ns3::UdpSocketFactory",
                             Address (InetSocketAddress (Ipv4Address::GetAny (), port)));
    apps = sink.Install (controlCenterNodes.Get(0));
    apps.Start (Seconds (1.0));
    apps.Stop (Seconds (5.0));

    Ptr<OutputStreamWrapper> routingStream = Create<OutputStreamWrapper> ("routing_table.routes", std::ios::out);
    staticRoutingPGW->PrintRoutingTable(routingStream);
    staticRoutingMIM2up->PrintRoutingTable(routingStream);
    staticRoutingSSup->PrintRoutingTable(routingStream);
    staticRoutingMIM1up->PrintRoutingTable(routingStream);
    

    AsciiTraceHelper ascii;
    p2ph.EnableAsciiAll (ascii.CreateFileStream ("backhaul.tr"));

    Simulator::Run ();
    Simulator::Destroy ();

    return 0;
    
/*
    //Ptr<Ipv4StaticRouting> remoteHostStaticRouting = ipv4RoutingHelper.GetStaticRouting(remoteHost->GetObject<Ipv4>());
    //remoteHostStaticRouting->AddNetworkRouteTo(Ipv4Address("7.0.0.0"), Ipv4Mask("255.0.0.0"), 1);
    
    
    
    // Defining node positions (for ueNodes and enbNodes). Everything we can
    //  handle with manually defined channel delays
    // Also defining names for nodes while I'm at it
    MobilityHelper mobility;
    
    Ptr<ListPositionAllocator> positionAllocUE = CreateObject<ListPositionAllocator> ();
    for (int idx=0; idx < ueNodes.GetN(); idx++){
        positionAllocUE->Add (Vector (nodeX[idx], nodeY[idx], 0.0));
        Names::Add(nodeName[idx], ueNodes.Get(idx));
    }
    mobility.SetMobilityModel ("ns3::ConstantPositionMobilityModel");
    mobility.SetPositionAllocator (positionAllocUE);
    mobility.Install (ueNodes);
    
    // Placing the eNB separately
    Ptr<ListPositionAllocator> positionAllocenb = CreateObject<ListPositionAllocator> ();
    positionAllocenb->Add (Vector (centerX, centerY, 0.0));
    mobility.SetPositionAllocator (positionAllocenb);
    mobility.Install (enbNodes);
    
    // Installing appropriate protocol stacks
    NetDeviceContainer enbDevs;
    enbDevs = lteHelper->InstallEnbDevice (enbNodes);
    NetDeviceContainer ueDevs;
    ueDevs = lteHelper->InstallUeDevice (ueNodes);
    
    //Installing IP stack on UEs
    internet.Install(ueNodes);

    Ipv4InterfaceContainer ueIpIface;
    // assign IP address to UEs
    
    for(int idx = 0; idx < ueNodes.GetN(); ++idx){
        Ptr<Node> ue = ueNodes.Get(idx);
        Ptr<NetDevice> ueLteDevice = ueDevs.Get(idx);
        ueIpIface = epcHelper->AssignUeIpv4Address(NetDeviceContainer(ueLteDevice));
        // set the default gateway for the UE
        Ptr<Ipv4StaticRouting> ueStaticRouting = ipv4RoutingHelper.GetStaticRouting(ue->GetObject<Ipv4>());
        ueStaticRouting->SetDefaultRoute(epcHelper->GetUeDefaultGatewayAddress(), 1);
      }
    
    //Attaching UE to eNB
    // When using EPC like we are this activates the default bearer automatically
    lteHelper->Attach (ueDevs);
    
    // Installing applications
    // UDPClient generate UDP packets that are received at the control center
    PacketSinkHelper packetSinkHelper ("ns3::UdpSocketFactory",
                                   InetSocketAddress(Ipv4Address::GetAny(), 1234));
    ApplicationContainer serverApps = packetSinkHelper.Install(controlCenterNodes);
    serverApps.Start(Seconds(0.01));
    UdpClientHelper client(ueIpIface.GetAddress(0), 1234); //Arbitrary node for testing
    ApplicationContainer clientApps = client.Install(ueNodes.Get(0));
    
    
    //Trace file setup
    AsciiTraceHelper ascii;
    p2ph.EnableAsciiAll (ascii.CreateFileStream ("LTE_ASCII.tr"));
    
    Simulator::Stop (Seconds (1));
    Simulator::Run ();
    Simulator::Destroy ();
*/
    return 0;
}
