/* -*-  Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil; -*- */
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
 * based on earlier integration work by Tom Henderson and Sam Jansen.
 * 2008 Florian Westphal <fw@strlen.de>
 */

#include "ns3/assert.h"
#include "ns3/log.h"
#include "ns3/nstime.h"

#include "ns3/packet.h"
#include "ns3/node.h"

#include "tcp-header.h"
#include "ipv4-end-point-demux.h"
#include "ipv4-end-point.h"
#include "ipv4-l3-protocol.h"
#include "nsc-tcp-l4-protocol.h"
#include "nsc-tcp-socket-impl.h"
#include "nsc-sysctl.h"

#include "tcp-typedefs.h"

#include <vector>
#include <sstream>
#include <dlfcn.h>
#include <iomanip>

#include <netinet/ip.h>
#include <netinet/tcp.h>

NS_LOG_COMPONENT_DEFINE ("NscTcpL4Protocol");

namespace ns3 {

NS_OBJECT_ENSURE_REGISTERED (NscTcpL4Protocol);

/* see http://www.iana.org/assignments/protocol-numbers */
const uint8_t NscTcpL4Protocol::PROT_NUMBER = 6;

ObjectFactory
NscTcpL4Protocol::GetDefaultRttEstimatorFactory (void)
{
  ObjectFactory factory;
  factory.SetTypeId (RttMeanDeviation::GetTypeId ());
  return factory;
}

TypeId 
NscTcpL4Protocol::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::NscTcpL4Protocol")
    .SetParent<Ipv4L4Protocol> ()

    .AddAttribute ("RttEstimatorFactory",
                   "How RttEstimator objects are created.",
                   ObjectFactoryValue (GetDefaultRttEstimatorFactory ()),
                   MakeObjectFactoryAccessor (&NscTcpL4Protocol::m_rttFactory),
                   MakeObjectFactoryChecker ())
    ;
  return tid;
}

int external_rand()
{
    return 1; // TODO
}

NscTcpL4Protocol::NscTcpL4Protocol ()
  : m_endPoints (new Ipv4EndPointDemux ()),
    m_nscStack (0),
    m_nscInterfacesSetUp(false),
    m_softTimer (Timer::CANCEL_ON_DESTROY)
{
  m_dlopenHandle = NULL;
  NS_LOG_FUNCTION_NOARGS ();
  NS_LOG_LOGIC("Made a NscTcpL4Protocol "<<this);
}

NscTcpL4Protocol::~NscTcpL4Protocol ()
{
  NS_LOG_FUNCTION_NOARGS ();
  dlclose(m_dlopenHandle);
}

void
NscTcpL4Protocol::SetNscLibrary(const std::string &soname)
{
  NS_ASSERT(!m_dlopenHandle);
  m_dlopenHandle = dlopen(soname.c_str (), RTLD_NOW);
  if (m_dlopenHandle == NULL)
    NS_FATAL_ERROR (dlerror());
}

void 
NscTcpL4Protocol::SetNode (Ptr<Node> node)
{
  m_node = node;

  if (m_nscStack)
    { // stack has already been loaded...
      return;
    }

  NS_ASSERT(m_dlopenHandle);

  FCreateStack create = (FCreateStack)dlsym(m_dlopenHandle, "nsc_create_stack");
  NS_ASSERT(create);
  m_nscStack = create(this, this, external_rand);
  int hzval = m_nscStack->get_hz();

  NS_ASSERT(hzval > 0);

  m_softTimer.SetFunction (&NscTcpL4Protocol::SoftInterrupt, this);
  m_softTimer.SetDelay (MilliSeconds (1000/hzval));
  m_nscStack->init(hzval);
  // This enables stack and NSC debug messages
  // m_nscStack->set_diagnostic(1000);

  Ptr<Ns3NscStack> nscStack = Create<Ns3NscStack> ();
  nscStack->SetStack (m_nscStack);
  node->AggregateObject (nscStack);

  m_softTimer.Schedule ();
}

int 
NscTcpL4Protocol::GetProtocolNumber (void) const
{
  return PROT_NUMBER;
}
int 
NscTcpL4Protocol::GetVersion (void) const
{
  return 2;
}

void
NscTcpL4Protocol::DoDispose (void)
{
  NS_LOG_FUNCTION_NOARGS ();
  if (m_endPoints != 0)
    {
      delete m_endPoints;
      m_endPoints = 0;
    }
  m_node = 0;
  Ipv4L4Protocol::DoDispose ();
}

Ptr<Socket>
NscTcpL4Protocol::CreateSocket (void)
{
  NS_LOG_FUNCTION_NOARGS ();
  if (!m_nscInterfacesSetUp)
  {
    Ptr<Ipv4> ip = m_node->GetObject<Ipv4> ();

    const uint32_t nInterfaces = ip->GetNInterfaces ();
    // start from 1, ignore the loopback interface (HACK)

    NS_ASSERT_MSG (nInterfaces <= 2, "nsc does not support multiple interfaces per node");

    for (uint32_t i = 1; i < nInterfaces; i++)
    {
      Ipv4Address addr = ip->GetAddress(i);
      Ipv4Mask mask = ip->GetNetworkMask(i);
      uint16_t mtu = ip->GetMtu (i);

      std::ostringstream addrOss, maskOss;

      addr.Print(addrOss);
      mask.Print(maskOss);

      NS_LOG_LOGIC ("if_attach " << addrOss.str().c_str() << " " << maskOss.str().c_str() << " " << mtu);

      std::string addrStr = addrOss.str();
      std::string maskStr = maskOss.str();
      const char* addrCStr = addrStr.c_str();
      const char* maskCStr = maskStr.c_str();
      m_nscStack->if_attach(addrCStr, maskCStr, mtu);

      if (i == 1)
      {
        // We need to come up with a default gateway here. Can't guarantee this to be
        // correct really...

        uint8_t addrBytes[4];
        addr.Serialize(addrBytes);

        // XXX: this is all a bit of a horrible hack
        //
        // Just increment the last octet, this gives a decent chance of this being
        // 'enough'.
        //
        // All we need is another address on the same network as the interface. This
        // will force the stack to output the packet out of the network interface.
        addrBytes[3]++;
        addr.Deserialize(addrBytes);
        addrOss.str("");
        addr.Print(addrOss);
        m_nscStack->add_default_gateway(addrOss.str().c_str());
      }
    }
    m_nscInterfacesSetUp = true;
  }

  Ptr<RttEstimator> rtt = m_rttFactory.Create<RttEstimator> ();
  Ptr<NscTcpSocketImpl> socket = CreateObject<NscTcpSocketImpl> ();
  socket->SetNode (m_node);
  socket->SetTcp (this);
  socket->SetRtt (rtt);
  return socket;
}

Ipv4EndPoint *
NscTcpL4Protocol::Allocate (void)
{
  NS_LOG_FUNCTION_NOARGS ();
  return m_endPoints->Allocate ();
}

Ipv4EndPoint *
NscTcpL4Protocol::Allocate (Ipv4Address address)
{
  NS_LOG_FUNCTION (this << address);
  return m_endPoints->Allocate (address);
}

Ipv4EndPoint *
NscTcpL4Protocol::Allocate (uint16_t port)
{
  NS_LOG_FUNCTION (this << port);
  return m_endPoints->Allocate (port);
}

Ipv4EndPoint *
NscTcpL4Protocol::Allocate (Ipv4Address address, uint16_t port)
{
  NS_LOG_FUNCTION (this << address << port);
  return m_endPoints->Allocate (address, port);
}

Ipv4EndPoint *
NscTcpL4Protocol::Allocate (Ipv4Address localAddress, uint16_t localPort,
                         Ipv4Address peerAddress, uint16_t peerPort)
{
  NS_LOG_FUNCTION (this << localAddress << localPort << peerAddress << peerPort);
  return m_endPoints->Allocate (localAddress, localPort,
                                peerAddress, peerPort);
}

void 
NscTcpL4Protocol::DeAllocate (Ipv4EndPoint *endPoint)
{
  NS_LOG_FUNCTION (this << endPoint);
  // NSC m_endPoints->DeAllocate (endPoint);
}

void
NscTcpL4Protocol::Receive (Ptr<Packet> packet,
             Ipv4Address const &source,
             Ipv4Address const &destination,
             Ptr<Ipv4Interface> incomingInterface)
{
  NS_LOG_FUNCTION (this << packet << source << destination << incomingInterface);
  Ipv4Header ipHeader;
  uint32_t packetSize = packet->GetSize();

  // The way things work at the moment, the IP header has been removed
  // by the ns-3 IPv4 processing code. However, the NSC stack expects
  // a complete IP packet, so we add the IP header back.
  // Since the original header is already gone, we create a new one
  // based on the information we have.
  ipHeader.SetSource (source);
  ipHeader.SetDestination (destination);
  ipHeader.SetProtocol (PROT_NUMBER);
  ipHeader.SetPayloadSize (packetSize);
  ipHeader.SetTtl (1);
  // all NSC stacks check the IP checksum
  ipHeader.EnableChecksum ();

  packet->AddHeader(ipHeader);
  packetSize = packet->GetSize();

  const uint8_t *data = const_cast<uint8_t *>(packet->PeekData());

  // deliver complete packet to the NSC network stack
  m_nscStack->if_receive_packet(0, data, packetSize);
  wakeup ();
}

void NscTcpL4Protocol::SoftInterrupt (void)
{
  NS_LOG_FUNCTION_NOARGS ();
  m_nscStack->timer_interrupt ();
  m_nscStack->increment_ticks ();
  m_softTimer.Schedule ();
}

void NscTcpL4Protocol::send_callback(const void* data, int datalen)
{
  Ptr<Packet> p;

  NS_ASSERT(datalen > (int)sizeof(struct iphdr));

  const uint8_t *rawdata = reinterpret_cast<const uint8_t *>(data);
  rawdata += sizeof(struct iphdr);

  const struct iphdr *ipHdr = reinterpret_cast<const struct iphdr *>(data);

  // create packet, without IP header. The TCP header is not touched.
  // Not using the IP header makes integration easier, but it destroys
  // eg. ECN.
  p = Create<Packet> (rawdata, datalen - sizeof(struct iphdr));

  Ipv4Address saddr(ntohl(ipHdr->saddr));
  Ipv4Address daddr(ntohl(ipHdr->daddr));

  Ptr<Ipv4L3Protocol> ipv4 = m_node->GetObject<Ipv4L3Protocol> ();
  if (ipv4 != 0)
    {
      ipv4->Send (p, saddr, daddr, PROT_NUMBER);
    }
  m_nscStack->if_send_finish(0);
}

void NscTcpL4Protocol::wakeup()
{
  // TODO
  // this should schedule a timer to read from all tcp sockets now... this is
  // an indication that data might be waiting on the socket

  Ipv4EndPointDemux::EndPoints endPoints = m_endPoints->GetAllEndPoints ();
  for (Ipv4EndPointDemux::EndPointsI endPoint = endPoints.begin ();
       endPoint != endPoints.end (); endPoint++) {
          // NSC HACK: (ab)use TcpSocket::ForwardUp for signalling
          (*endPoint)->ForwardUp (NULL, Ipv4Address(), 0);
  }
}

void NscTcpL4Protocol::gettime(unsigned int* sec, unsigned int* usec)
{
  // Only used by the Linux network stack, e.g. during ISN generation
  // and in the kernel rng initialization routine. Also used in Linux
  // printk output.
  Time t = Simulator::Now ();
  int64_t us = t.GetMicroSeconds ();
  *sec = us / (1000*1000);
  *usec = us - *sec * (1000*1000);
}


}; // namespace ns3

