/**
 * @file propagation-models-wifi.cc
 * @author Sophia Nowicki
 */

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/flow-monitor-helper.h"
#include "ns3/internet-module.h"
#include "ns3/ipv4-flow-classifier.h"
#include "ns3/mobility-helper.h"
#include "ns3/mobility-model.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/wifi-module.h"
#include "ns3/yans-wifi-channel.h"
#include "ns3/yans-wifi-helper.h"

#include <fstream>
#include <iostream>

using namespace ns3;
using namespace std;

double distanceIncrement = 1.0;
double rss = 0;
int model = 0;

vector<string> propagationModels = {"FriisPropagationLossModel",
                                    "FixedRssLossModel",
                                    "ThreeLogDistancePropagationLossModel",
                                    "TwoRayGroundPropagationLossModel",
                                    "NakagamiPropagationLossModel"};

void
RssCallback(Ptr<const Packet> packet,
            uint16_t channelFreqMhz,
            WifiTxVector txVector,
            MpduInfo aMpdu,
            SignalNoiseDbm signalNoise,
            uint16_t stadId)
{
    rss = signalNoise.signal;
};

int
main(int argc, char* argv[])
{
    LogComponentEnable("YansWifiChannel", LOG_LEVEL_ALL);
    // Simulation parameters
    Time::SetResolution(Time::NS); // nanoseconds
    double throughput_server = 1;
    double throughput = 1;
    double distance = 5;
    double z1 = 0;
    double z2 = 0;
    double interval = 0.0001547;
    double simulationTime = 3;
    // Command line options
    CommandLine cmd(__FILE__);
    cmd.AddValue("model", "index of propagation loss model", model);
    cmd.AddValue("increment", "increment distance by this number", distanceIncrement);
    cmd.AddValue("time", "simulation time", simulationTime);
    cmd.Parse(argc, argv);

    // Creating .csv file for each propagation model
    ofstream csv;
    string fileName = "new_stats_" + propagationModels[model] + ".csv";
    csv.open(fileName);
    csv << "Simulation Time,Packet Interval"
        << "\n";
    csv << "model: " << propagationModels[model] << "\n";
    csv << simulationTime << "," << interval << "\n";
    csv << "distance [m],rss [dBm],throughput [Mbps]"
        << "\n";
    csv.close();

    // Incrementing distance until measured throughput = 0
    while (throughput_server != 0)
    {
        NS_LOG_UNCOND("Setting physical layer for propagation model " + propagationModels[model] +
                      "...");
        NodeContainer nodes;
        nodes.Create(2);

        // Mobility
        MobilityHelper mobility;
        Ptr<ListPositionAllocator> positionAllocator = CreateObject<ListPositionAllocator>();
        positionAllocator->Add(Vector(0.0, 0.0, z1));
        positionAllocator->Add(Vector(distance, 0.0, z2));
        mobility.SetPositionAllocator(positionAllocator);
        mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
        mobility.Install(nodes);

        WifiHelper wifi;
        wifi.SetStandard(WIFI_STANDARD_80211n);

        YansWifiPhyHelper wifiPhy;
        // Set antenna gain of 1 dbi
        wifiPhy.Set("RxGain", DoubleValue(1));
        wifiPhy.Set("TxGain", DoubleValue(1));
        // Set transmission power of 10 dbm
        wifiPhy.Set("TxPowerStart", DoubleValue(10));
        wifiPhy.Set("TxPowerEnd", DoubleValue(10));
        /* Set channel settings: {channel number, channel width, frequency band, primary20
        index} 0 for corresponding default settings. Use channel width 40 for achieving data
        rate of 75 Mbps in 5 GHz frequency band for 802.11n */
        wifiPhy.Set("ChannelSettings", StringValue("{0, 40, BAND_5GHZ, 0}"));

        YansWifiChannelHelper wifiChannel;
        wifiChannel.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel");
        switch (model)
        {
        // FixedRssLossModel
        case 1:
            wifiChannel.AddPropagationLoss("ns3::" + propagationModels[model],
                                           "Rss",
                                           DoubleValue(-80));
            break;
        // TwoRayGroundPropagationLossModel
        case 3:
            wifiChannel.AddPropagationLoss(
                "ns3::" + propagationModels[model],
                "HeightAboveZ",
                DoubleValue(1)); // height of the antenna (m) above the node's z coordinate
            z1 = 1;
            z2 = 1;
            break;
        // Setting channel for FriisPropagationLossModel,ThreeLogDistanceLossModel and
        // NakagamiPropagationLossModel
        default:
            wifiChannel.AddPropagationLoss("ns3::" + propagationModels[model]);
            break;
        }
        wifiPhy.SetChannel(wifiChannel.Create());

        NS_LOG_UNCOND("Wifi 802.11n physical channel configured.");

        // MAC-Layer
        WifiMacHelper wifiMac;
        wifiMac.SetType("ns3::AdhocWifiMac");

        // Server on node 0 and client on node 1
        NetDeviceContainer devices = wifi.Install(wifiPhy, wifiMac, nodes);
        // NetDeviceContainer clientDevice = wifi.Install(wifiPhy, wifiMac, nodes.Get(1));

        // Configure callback for trace sources
        Config::ConnectWithoutContext(
            "/NodeList/0/DeviceList/*/$ns3::WifiNetDevice/Phy/MonitorSnifferRx",
            MakeCallback(&RssCallback));
        // IP
        InternetStackHelper internetHelper;
        internetHelper.Install(nodes);

        NS_LOG_UNCOND("Assign IP Addresses...");
        Ipv4AddressHelper ipv4;
        ipv4.SetBase("10.1.1.0", "255.255.255.0");

        // 0'th interface in this container corresponds to IP of 0'th node in nodes-container
        Ipv4InterfaceContainer ipInterfaces = ipv4.Assign(devices);

        // Generate UDP traffic
        NS_LOG_UNCOND("Create UDP server application on node 0.");
        UdpServerHelper server(9);
        ApplicationContainer serverApps = server.Install(nodes.Get(1));
        serverApps.Start(Seconds(1.0)); // start application at 1 second into simulation
        serverApps.Stop(Seconds(simulationTime));

        NS_LOG_UNCOND("Create UDP client on node 1 to send to node 0.");
        UdpClientHelper client(ipInterfaces.GetAddress(1), 9);
        uint32_t packetSize = 1450;
        client.SetAttribute("MaxPackets", UintegerValue(4294967295U));
        // Time interval = packetSize / datarate
        // approximately 0.0001547
        Time packetInterval = Seconds(interval);
        client.SetAttribute("Interval", TimeValue(packetInterval));
        client.SetAttribute("PacketSize", UintegerValue(packetSize));
        ApplicationContainer clientApps = client.Install(nodes.Get(0));
        clientApps.Start(Seconds(2.0)); // start application at 2 seconds into simulation
        clientApps.Stop(Seconds(simulationTime));

        // // Enable IP flow monitoring
        FlowMonitorHelper flowMonitor;
        Ptr<FlowMonitor> monitor = flowMonitor.InstallAll();

        // Run simulation
        Simulator::Stop(Seconds(simulationTime + 1));
        Simulator::Run();
        monitor->CheckForLostPackets();

        uint64_t totalPacketsThrough = DynamicCast<UdpServer>(serverApps.Get(0))->GetReceived();
        double throughput_server =
            totalPacketsThrough * packetSize * 8 / (simulationTime * 1000000.0); // Mbit/s
        // Retrieve statistics from FlowMonitor interface
        FlowMonitor::FlowStatsContainer stats = monitor->GetFlowStats();
        Ptr<Ipv4FlowClassifier> classifier =
            DynamicCast<Ipv4FlowClassifier>(flowMonitor.GetClassifier());

        for (std::map<FlowId, FlowMonitor::FlowStats>::const_iterator it = stats.begin();
             it != stats.end();
             ++it)
        {
            Ipv4FlowClassifier::FiveTuple tuple = classifier->FindFlow(it->first);

            // calculate throughput in Mbps
            throughput = it->second.rxBytes * 8.0 /
                         (it->second.timeLastRxPacket.GetSeconds() -
                          it->second.timeFirstTxPacket.GetSeconds()) /
                         1024 / 1024;

            cout << "Source: " << tuple.sourceAddress << "\n"
                 << "Destination: " << tuple.destinationAddress << "\n"
                 << "Transmitted bytes: " << it->second.txBytes << "\n"
                 << "Received bytes: " << it->second.rxBytes << "\n"
                 << "Throughput Mbps: " << throughput << "\n"
                 << "RSS: " << rss << endl;
        }
        Simulator::Destroy();
        csv.open(fileName, ios::app);
        csv << distance << "," << rss << "," << throughput_server << "\n";
        csv.close();
        distance += distanceIncrement;
    }
    return 0;
}
