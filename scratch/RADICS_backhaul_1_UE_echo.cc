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
#include <cassert>
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/csma-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/ipv4-static-routing-helper.h"
#include "ns3/error-model.h"
#include "ns3/names.h"
#include "ns3/node-list.h"
#include "ns3/fncs-application.h"
#include "ns3/fncs-application-helper.h"
#include "ns3/fncs-simulator-impl.h"
#include "ns3/mobility-module.h"
#include "ns3/lte-module.h"




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

int 
main (int argc, char *argv[])
{
    // Allow the user to override any of the defaults and the above
    // DefaultValue::Bind ()s at run-time, via command-line arguments
    CommandLine cmd;
    cmd.Parse (argc, argv);
    
    Config::SetDefault ("ns3::LteEnbRrc::SrsPeriodicity", UintegerValue (320));
    
    LogComponentEnable("RADICS_SM", LOG_LEVEL_ALL);
    //LogComponentEnable("LteUeRrc", LOG_LEVEL_ALL);
    //LogComponentEnable("LteEnbRrc", LOG_LEVEL_ALL);
    //LogComponentEnable("LteUeNetDevice", LOG_LEVEL_ALL);
    //LogComponentEnable("EpcSgwPgwApplication", LOG_LEVEL_ALL);
    //LogComponentEnable("EpcEnbApplication", LOG_LEVEL_ALL);
    //LogComponentEnable("Ipv4AddressHelper", LOG_LEVEL_ALL);
    LogComponentEnable("OnOffApplication", LOG_LEVEL_ALL);
    LogComponentEnable("Ipv4StaticRouting", LOG_LEVEL_ALL);
    //LogComponentEnable("PointToPointChannel", LOG_LEVEL_ALL);
    //LogComponentEnable("PointToPointNetDevice", LOG_LEVEL_ALL);
    //LogComponentEnable("PointToPointNetDevice", LOG_LEVEL_ALL);
    //LogComponentEnable("Ipv4InterfaceAddress", LOG_LEVEL_LOGIC);

    // *************************************************************************
    // Defining UE (smart meter) node locations.
    //
    
    // Reading in list of nodes and their locations
    std::vector<std::string> nodeName;
    std::vector<double> nodeX;
    std::vector<double> nodeY;

    std::ifstream  topoFile("/Users/hard312/models/Multi-source sub (RADICS)/R4-12.47-1_sm_positions_m_short.csv");
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
            NS_LOG_INFO("Substation coordinates: " << subX << ", " << subY);
        }
        
        // Finding triplex meters (smart meters)
        std::string tm = row[0].substr(11,2);
        if (tm.compare("tm") == 0) { // row is a triplex meter
            nodeName.push_back(row[0]);
            nodeX.push_back(nodeX_double);
            nodeY.push_back(nodeY_double);
            NS_LOG_DEBUG("Smart meter node name: " << nodeName.back() << "\tNode X: " << nodeX.back() << "\tNode Y: " << nodeY.back());
        }
    }
    int numMeters = nodeName.size();
    NS_LOG_INFO("Number of triplex meters: " << numMeters);
    
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
    double centerX = maxX-minX/2;
    double centerY = maxY-minY/2;
    NS_LOG_INFO("Geographic center of feeder: " << centerX << ", " << centerY);


    // *************************************************************************
    // Building network
    //
    
    MobilityHelper mobility;
    InternetStackHelper internet;
    Ptr<LteHelper> lteHelper = CreateObject<LteHelper>();
    Ptr<PointToPointEpcHelper> epcHelper = CreateObject<PointToPointEpcHelper>();
    lteHelper->SetEpcHelper(epcHelper);
    
    // Defining all the wireless and wired nodes
    NS_LOG_INFO("Creating nodes");
    Ptr<Node> nCC = CreateObject<Node> ();
    Ptr<Node> nMIM1 = CreateObject<Node> ();
    Ptr<Node> nSS = CreateObject<Node> ();
    Ptr<Node> nMIM2 = CreateObject<Node> ();
    Ptr<Node> nPGW = epcHelper->GetPgwNode();
    NodeContainer ncCC_MIM1 = NodeContainer (nCC, nMIM1);
    NodeContainer ncMIM1_SS = NodeContainer (nMIM1, nSS);
    NodeContainer ncSS_MIM2 = NodeContainer (nSS, nMIM2);
    NodeContainer ncMIM2_PGW = NodeContainer (nMIM2, nPGW);
    NodeContainer backhaulNodes = NodeContainer (nCC, nSS, nMIM1, nMIM2);
    NodeContainer ncUE;
    ncUE.Create(numMeters);
    NodeContainer ncENB;
    ncENB.Create(3);

    internet.Install (backhaulNodes);
    
    
    // ************************************************************************
    //          Building backhaul network
    //
    // Point-to-point links
    NS_LOG_INFO("Creating backhaul point-to-point channels.");
    // Defining channels
    PointToPointHelper p2p;
    p2p.SetDeviceAttribute ("DataRate", StringValue ("5Mbps"));
    p2p.SetChannelAttribute ("Delay", StringValue ("2ms"));
    NetDeviceContainer dcCC_MIM1 = p2p.Install (ncCC_MIM1);
    NetDeviceContainer dcMIM1_SS = p2p.Install (ncMIM1_SS);
    NetDeviceContainer dcSS_MIM2 = p2p.Install (ncSS_MIM2);
    NetDeviceContainer dcMIM2_PGW = p2p.Install (ncMIM2_PGW);

    // Defining IP addresses.
    NS_LOG_INFO("Assigning IP addresses.");
    Ipv4AddressHelper ipv4;
    ipv4.SetBase ("10.1.1.0", "255.255.255.0", "0.0.0.1");
    Ipv4InterfaceContainer ifcCC_MIM1 = ipv4.Assign (dcCC_MIM1);
    ipv4.SetBase ("10.1.1.0", "255.255.255.0", "0.0.0.3");
    Ipv4InterfaceContainer ifcMIM1_SS = ipv4.Assign (dcMIM1_SS);
    ipv4.SetBase ("10.1.1.0", "255.255.255.0", "0.0.0.5");
    Ipv4InterfaceContainer ifcSS_MIM2 = ipv4.Assign (dcSS_MIM2);
    ipv4.SetBase ("10.1.1.0", "255.255.255.0", "0.0.0.7");
    Ipv4InterfaceContainer ifcMIM2_PGW = ipv4.Assign (dcMIM2_PGW);

    NS_LOG_INFO("Defining backhaul IPv4 pointers.");
    Ptr<Ipv4> ipv4CC = nCC->GetObject<Ipv4> ();
    Ptr<Ipv4> ipv4MIM1 = nMIM1->GetObject<Ipv4> ();
    Ptr<Ipv4> ipv4SS = nSS->GetObject<Ipv4> ();
    Ptr<Ipv4> ipv4MIM2 = nMIM2->GetObject<Ipv4> ();
    Ptr<Ipv4> ipv4PGW = nPGW->GetObject<Ipv4> ();

    NS_LOG_INFO("Defining backhaul IPv4 static routing pointers.");
    Ipv4StaticRoutingHelper ipv4RoutingHelper;
    // Declaring the static routing objects for all nodes that will need to act as routers
    Ptr<Ipv4StaticRouting> staticRoutingCC = ipv4RoutingHelper.GetStaticRouting(ipv4CC);
    Ptr<Ipv4StaticRouting> staticRoutingMIM1 = ipv4RoutingHelper.GetStaticRouting(ipv4MIM1);
    Ptr<Ipv4StaticRouting> staticRoutingSS = ipv4RoutingHelper.GetStaticRouting(ipv4SS);
    Ptr<Ipv4StaticRouting> staticRoutingMIM2 = ipv4RoutingHelper.GetStaticRouting(ipv4MIM2);
    Ptr<Ipv4StaticRouting> staticRoutingPGW = ipv4RoutingHelper.GetStaticRouting(ipv4PGW);

    NS_LOG_INFO("Defining backhaul downlink static routes.");
    // Routes from CC to PGW
    /*
    // Working - As if MIM nodes were legitimate
    staticRoutingCC->AddHostRouteTo (Ipv4Address (ifcMIM2_PGW.GetAddress(1)), ifcCC_MIM1.GetAddress(1), 1);
    staticRoutingMIM1->AddHostRouteTo (Ipv4Address (ifcMIM2_PGW.GetAddress(1)), ifcMIM1_SS.GetAddress(1), 2);
    staticRoutingSS->AddHostRouteTo (Ipv4Address (ifcMIM2_PGW.GetAddress(1)), ifcSS_MIM2.GetAddress(1), 2);
    staticRoutingMIM2->AddHostRouteTo (Ipv4Address (ifcMIM2_PGW.GetAddress(1)), ifcMIM2_PGW.GetAddress(1), 2);
    */
    // As if MIM nodes were invisible
    staticRoutingCC->AddHostRouteTo (Ipv4Address (ifcMIM2_PGW.GetAddress(1)), ifcMIM1_SS.GetAddress(1), 1);
    staticRoutingMIM1->AddHostRouteTo (Ipv4Address (ifcMIM2_PGW.GetAddress(1)), ifcMIM1_SS.GetAddress(1), 2);
    staticRoutingSS->AddHostRouteTo (Ipv4Address (ifcMIM2_PGW.GetAddress(1)), ifcMIM2_PGW.GetAddress(1), 2);
    staticRoutingMIM2->AddHostRouteTo (Ipv4Address (ifcMIM2_PGW.GetAddress(1)), ifcMIM2_PGW.GetAddress(1), 2);
    
    NS_LOG_INFO("Defining backhaul uplink static routes.");
    // Routes from PGW to CC
    /*
    // As if MIM nodes were legitimate
    staticRoutingPGW->AddHostRouteTo (Ipv4Address (ifcCC_MIM1.GetAddress(0)), ifcMIM2_PGW.GetAddress(0), 1);
    staticRoutingMIM2->AddHostRouteTo (Ipv4Address (ifcCC_MIM1.GetAddress(0)), ifcSS_MIM2.GetAddress(0), 1);
    staticRoutingSS->AddHostRouteTo (Ipv4Address (ifcCC_MIM1.GetAddress(0)), ifcMIM1_SS.GetAddress(0), 1);
    staticRoutingSS->AddHostRouteTo (Ipv4Address (ifcCC_MIM1.GetAddress(0)), ifcCC_MIM1.GetAddress(0), 1);
    */
    // As if MIM nodes were invisible
    staticRoutingPGW->AddHostRouteTo (Ipv4Address (ifcCC_MIM1.GetAddress(0)), ifcSS_MIM2.GetAddress(0), 2);
    staticRoutingMIM2->AddHostRouteTo (Ipv4Address (ifcCC_MIM1.GetAddress(0)), ifcSS_MIM2.GetAddress(0), 1);
    staticRoutingSS->AddHostRouteTo (Ipv4Address (ifcCC_MIM1.GetAddress(0)), ifcCC_MIM1.GetAddress(0), 1);
    staticRoutingMIM1->AddHostRouteTo (Ipv4Address (ifcCC_MIM1.GetAddress(0)), ifcCC_MIM1.GetAddress(0), 1);
    
    NS_LOG_INFO("Defining backhaul static routes to UE.");
    // Routes from backhaul to UE network (wireless connection through PGW and ENB)
    staticRoutingCC->AddNetworkRouteTo (Ipv4Address("7.0.0.0"), Ipv4Mask("255.0.0.0"), 1);
    staticRoutingMIM2->AddNetworkRouteTo (Ipv4Address("7.0.0.0"), Ipv4Mask("255.0.0.0"), 2);
    staticRoutingSS->AddNetworkRouteTo (Ipv4Address("7.0.0.0"), Ipv4Mask("255.0.0.0"), 2);
    staticRoutingMIM1->AddNetworkRouteTo (Ipv4Address("7.0.0.0"), Ipv4Mask("255.0.0.0"), 2);
    
    // Adding route from PGW wireless network to backhaul network (this may be redundant)
    staticRoutingPGW->AddNetworkRouteTo(Ipv4Address("10.1.1.0"), Ipv4Mask("255.255.255.0"), 1);

    // ************************************************************************
    //          Building wireless network
    //
    
    // Define locations for wireless nodes
    NS_LOG_INFO("Defining UE node positions.");
    Ptr<ListPositionAllocator> positionAllocUE = CreateObject<ListPositionAllocator>();
    //NS_LOG_INFO("ncUE has node count: " << ncUE.GetN());
    for (int idx=0; idx < ncUE.GetN(); idx++){
        positionAllocUE->Add(Vector (nodeX[idx], nodeY[idx], 0.0));
        Names::Add(nodeName[idx], ncUE.Get(idx));
    }
    mobility.SetMobilityModel ("ns3::ConstantPositionMobilityModel");
    mobility.SetPositionAllocator (positionAllocUE);
    mobility.Install(ncUE);
    
    // Placing the eNB separately
    NS_LOG_INFO("Defining ENB positions.");
    double x0 = centerX;
    double x1 = centerX-(centerX/2);
    double x2 = centerX+(centerX/2);
    double y0 = centerY;
    double y1 = centerY-(centerY/2);
    double y2 = centerY+(centerY/2);
    Ptr<ListPositionAllocator> positionAllocENB = CreateObject<ListPositionAllocator> ();
    positionAllocENB->Add (Vector (x0, y0, 0.0));
    positionAllocENB->Add (Vector (x1, y1, 0.0));
    positionAllocENB->Add (Vector (x2, y2, 0.0));
    mobility.SetPositionAllocator(positionAllocENB);
    mobility.Install(ncENB);

    //Creating net devices
    NS_LOG_INFO("Creating net devices for UEs and ENBs.");
    NetDeviceContainer dcENB, dcUE;
    dcENB = lteHelper->InstallEnbDevice(ncENB);
    dcUE = lteHelper->InstallUeDevice(ncUE);
    
    internet.Install(ncUE);
    
    Ipv4InterfaceContainer ueIpIface = epcHelper->AssignUeIpv4Address(NetDeviceContainer(dcUE));
    NS_LOG_INFO("Assigning IPs and creating static routing for UEs.");
    for (int idx=0; idx < ncUE.GetN(); idx++){
        Ptr<Node> nUE = ncUE.Get(idx);
        // set the default gateway for the UE
        Ptr<Ipv4StaticRouting> ueStaticRouting = ipv4RoutingHelper.GetStaticRouting (nUE->GetObject<Ipv4> ());
        ueStaticRouting->SetDefaultRoute(epcHelper->GetUeDefaultGatewayAddress(), 1);
    }
    
    // Attach UE to ENBs (automatically finds the strongest ENB for attachment
    lteHelper->Attach (dcUE);



    
    // *************************************************************************
    //          Installing applications
    //
    
    /*
    // Create the OnOff application to send UDP datagrams of size
    // 210 bytes at a rate of 448 Kb/s
    uint16_t port = 9;   // Discard port (RFC 863)
    OnOffHelper onoff ("ns3::UdpSocketFactory", 
                     Address (InetSocketAddress (ifcMIM2_PGW.GetAddress(1), port)));
    onoff.SetConstantRate (DataRate (6000));
    ApplicationContainer apps = onoff.Install (nCC);
    apps.Start (Seconds (1.0));
    apps.Stop (Seconds (3.0));

    // Create a packet sink to receive these packets
    PacketSinkHelper sink ("ns3::UdpSocketFactory",
                         Address (InetSocketAddress (Ipv4Address::GetAny (), port)));
    apps = sink.Install (nPGW);
    apps.Start (Seconds (1.0));
    apps.Stop (Seconds (5.0));
    */
    //Echo server recieves and echos back packets.
    NS_LOG_INFO("Installing echo server.");
    UdpEchoServerHelper echoServer (9); //port number
    ApplicationContainer acEchoServer = echoServer.Install (ncUE.Get(0));
    acEchoServer.Start (Seconds (1.0));
    acEchoServer.Stop (Seconds (10.0));
    
    //Echo client generates packets (which are echoed back by the server)
    NS_LOG_INFO("Installing echo client.");
    UdpEchoClientHelper echoClient (ueIpIface.GetAddress(0), 9);
    echoClient.SetAttribute ("MaxPackets", UintegerValue (1));
    echoClient.SetAttribute ("Interval", TimeValue (Seconds (11)));
    echoClient.SetAttribute ("PacketSize", UintegerValue (1024));
    ApplicationContainer acEchoClient = echoClient.Install(nCC);
    acEchoClient.Start (Seconds (1.0));
    acEchoClient.Stop (Seconds (10));
    
    
    // *************************************************************************
    //          Setting up results recorders
    //
    
    NS_LOG_INFO("Initializing output files.");
    AsciiTraceHelper ascii;
    p2p.EnableAsciiAll (ascii.CreateFileStream ("static_routing_test.tr"));
    //p2p.EnablePcapAll ("static-routing-slash32");

    Ptr<OutputStreamWrapper> routingStream = Create<OutputStreamWrapper> ("static_routing_table.routes", std::ios::out);
    staticRoutingCC->PrintRoutingTable(routingStream);
    staticRoutingMIM1->PrintRoutingTable(routingStream);
    staticRoutingSS->PrintRoutingTable(routingStream);
    staticRoutingMIM2->PrintRoutingTable(routingStream);
    staticRoutingPGW->PrintRoutingTable(routingStream);


    NS_LOG_INFO("Running simulation.");
    Simulator::Run ();
    Simulator::Destroy ();

    return 0;
}
