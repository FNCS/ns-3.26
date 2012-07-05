/* -*-  Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2011 The Boeing Company
 *
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
 * Author: Gary Pei <guangyu.pei@boeing.com>
 */
#include "ns3/log.h"
#include "ns3/abort.h"
#include <math.h>
#include "ns3/simulator.h"
#include "ns3/lr-wpan-phy.h"
#include "ns3/trace-source-accessor.h"

NS_LOG_COMPONENT_DEFINE ("LrWpanPhy");

namespace ns3 {

NS_OBJECT_ENSURE_REGISTERED (LrWpanPhy);

// Table 22 in section 6.4.1 of ieee802.15.4
const uint32_t LrWpanPhy::aMaxPhyPacketSize = 127; // max PSDU in octets
const uint32_t LrWpanPhy::aTurnaroundTime = 12;  // RX-to-TX or TX-to-RX in symbol periods

// IEEE802.15.4-2006 Table 2 in section 6.1.2 (kb/s and ksymbol/s)
// The index follows LrWpanPhyOption
const LrWpanPhyDataAndSymbolRates
LrWpanPhy::dataSymbolRates[7] = { { 20.0, 20.0},
                                  { 40.0, 40.0},
                                  { 250.0, 12.5},
                                  { 250.0, 50.0},
                                  { 100.0, 25.0},
                                  { 250.0, 62.5},
                                  { 250.0, 62.5}};
// IEEE802.15.4-2006 Table 19 and Table 20 in section 6.3.
// The PHR is 1 octet and it follows phySymbolsPerOctet in Table 23
// The index follows LrWpanPhyOption
const LrWpanPhyPpduHeaderSymbolNumber
LrWpanPhy::ppduHeaderSymbolNumbers[7] = { { 32.0, 8.0, 8.0},
                                          { 32.0, 8.0, 8.0},
                                          { 2.0, 1.0, 0.4},
                                          { 6.0, 1.0, 1.6},
                                          { 8.0, 2.0, 2.0},
                                          { 8.0, 2.0, 2.0},
                                          { 8.0, 2.0, 2.0}};

TypeId
LrWpanPhy::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::LrWpanPhy")
    .SetParent<Object> ()
    .AddConstructor<LrWpanPhy> ()
    .AddTraceSource ("TrxState",
                     "The state of the transceiver",
                     MakeTraceSourceAccessor (&LrWpanPhy::m_trxStateLogger))
    .AddTraceSource ("PhyTxBegin",
                     "Trace source indicating a packet has begun transmitting over the channel medium",
                     MakeTraceSourceAccessor (&LrWpanPhy::m_phyTxBeginTrace))
    .AddTraceSource ("PhyTxEnd",
                     "Trace source indicating a packet has been completely transmitted over the channel.",
                     MakeTraceSourceAccessor (&LrWpanPhy::m_phyTxEndTrace))
    .AddTraceSource ("PhyTxDrop",
                     "Trace source indicating a packet has been dropped by the device during transmission",
                     MakeTraceSourceAccessor (&LrWpanPhy::m_phyTxDropTrace))
    .AddTraceSource ("PhyRxBegin",
                     "Trace source indicating a packet has begun being received from the channel medium by the device",
                     MakeTraceSourceAccessor (&LrWpanPhy::m_phyRxBeginTrace))
    .AddTraceSource ("PhyRxEnd",
                     "Trace source indicating a packet has been completely received from the channel medium by the device",
                     MakeTraceSourceAccessor (&LrWpanPhy::m_phyRxEndTrace))
    .AddTraceSource ("PhyRxDrop",
                     "Trace source indicating a packet has been dropped by the device during reception",
                     MakeTraceSourceAccessor (&LrWpanPhy::m_phyRxDropTrace))
  ;
  return tid;
}

LrWpanPhy::LrWpanPhy ()
  : m_edRequest (),
    m_setTRXState ()
{
  m_trxState = IEEE_802_15_4_PHY_IDLE;
  ChangeTrxState (IEEE_802_15_4_PHY_RX_ON);
  m_trxStatePending = IEEE_802_15_4_PHY_IDLE;

  // default PHY PIB attributes
  m_phyPIBAttributes.phyCurrentChannel = 11;
  m_phyPIBAttributes.phyTransmitPower = 0;
  m_phyPIBAttributes.phyCurrentPage = 0;
  for (uint32_t i = 0; i < 32; i++)
    {
      m_phyPIBAttributes.phyChannelsSupported[i] = 0x07ffffff;
    }
  m_phyPIBAttributes.phyCCAMode = 1;

  SetMyPhyOption ();

  m_rxEdPeakPower = 0.0;
  m_rxTotalPower = 0.0;
  m_rxTotalNum = 0;
  // default -85 dBm in W for 2.4 GHz
  m_rxSensitivity = pow (10.0, -85.0 / 10.0) / 1000.0;
  LrWpanSpectrumValueHelper psdHelper;
  m_txPsd = psdHelper.CreateTxPowerSpectralDensity (m_phyPIBAttributes.phyTransmitPower,
                                                    m_phyPIBAttributes.phyCurrentChannel);
  m_noise = psdHelper.CreateNoisePowerSpectralDensity (m_phyPIBAttributes.phyCurrentChannel);
  m_rxPsd = 0;
  Ptr <Packet> none = 0;
  m_currentRxPacket = std::make_pair (none, true);
  m_currentTxPacket = std::make_pair (none, true);
  m_errorModel = 0;
}

LrWpanPhy::~LrWpanPhy ()
{
}

void
LrWpanPhy::DoDispose ()
{
  NS_LOG_FUNCTION (this);
  m_mobility = 0;
  m_device = 0;
  m_channel = 0;
  m_txPsd = 0;
  m_rxPsd = 0;
  m_noise = 0;
  m_errorModel = 0;
  m_pdDataIndicationCallback = MakeNullCallback< void, uint32_t, Ptr<Packet>, uint32_t > ();
  m_pdDataConfirmCallback = MakeNullCallback< void, LrWpanPhyEnumeration > ();
  m_plmeCcaConfirmCallback = MakeNullCallback< void, LrWpanPhyEnumeration > ();
  m_plmeEdConfirmCallback = MakeNullCallback< void, LrWpanPhyEnumeration,uint8_t > ();
  m_plmeGetAttributeConfirmCallback = MakeNullCallback< void, LrWpanPhyEnumeration, LrWpanPibAttributeIdentifier, LrWpanPhyPIBAttributes* > ();
  m_plmeSetTRXStateConfirmCallback = MakeNullCallback< void, LrWpanPhyEnumeration > ();
  m_plmeSetAttributeConfirmCallback = MakeNullCallback< void, LrWpanPhyEnumeration, LrWpanPibAttributeIdentifier > ();

  SpectrumPhy::DoDispose ();
}

Ptr<NetDevice>
LrWpanPhy::GetDevice ()
{
  NS_LOG_FUNCTION (this);
  return m_device;
}


Ptr<MobilityModel>
LrWpanPhy::GetMobility ()
{
  NS_LOG_FUNCTION (this);
  return m_mobility;
}


void
LrWpanPhy::SetDevice (Ptr<NetDevice> d)
{
  NS_LOG_FUNCTION (this << d);
  m_device = d;
}


void
LrWpanPhy::SetMobility (Ptr<MobilityModel> m)
{
  NS_LOG_FUNCTION (this << m);
  m_mobility = m;
}


void
LrWpanPhy::SetChannel (Ptr<SpectrumChannel> c)
{
  NS_LOG_FUNCTION (this << c);
  m_channel = c;
}


Ptr<SpectrumChannel>
LrWpanPhy::GetChannel (void)
{
  return m_channel;
}


Ptr<const SpectrumModel>
LrWpanPhy::GetRxSpectrumModel () const
{
  if (m_txPsd)
    {
      return m_txPsd->GetSpectrumModel ();
    }
  else
    {
      return 0;
    }
}

Ptr<AntennaModel>
LrWpanPhy::GetRxAntenna ()
{
  return m_antenna;
}

void
LrWpanPhy::SetAntenna (Ptr<AntennaModel> a)
{
  NS_LOG_FUNCTION (this << a);
  m_antenna = a;
}



void
LrWpanPhy::StartRx (Ptr<SpectrumSignalParameters> spectrumRxParams)
{
  NS_LOG_FUNCTION (this << spectrumRxParams);
  LrWpanSpectrumValueHelper psdHelper;


  Ptr<LrWpanSpectrumSignalParameters> lrWpanRxParams = DynamicCast<LrWpanSpectrumSignalParameters> (spectrumRxParams);

  if (lrWpanRxParams != 0)
    {
      // The specification doesn't seem to refer to BUSY_RX, but vendor
      // data sheets suggest that this is a substate of the RX_ON state
      // that is entered after preamble detection when the digital receiver
      // is enabled.  Here, for now, we use BUSY_RX to mark the period between
      // StartRx() and EndRx() states.
      ChangeTrxState (IEEE_802_15_4_PHY_BUSY_RX);

      Time duration = lrWpanRxParams->duration;

      Ptr<Packet> p = (lrWpanRxParams->packetBurst->GetPackets ()).front ();
      m_currentRxPacket = std::make_pair (p, false);
      m_rxPsd = lrWpanRxParams->psd;
      m_rxTotalPower = psdHelper.TotalAvgPower (*m_rxPsd);
      Simulator::Schedule (duration, &LrWpanPhy::EndRx, this);
      m_phyRxBeginTrace (p);
    }
  NS_LOG_LOGIC (this << " state: " << m_trxState << " m_rxTotalPower: " << m_rxTotalPower);
}

void LrWpanPhy::EndRx ()
{
  NS_LOG_FUNCTION (this);
  LrWpanSpectrumValueHelper psdHelper;
  // Calculate whether packet was lost
  if (m_currentRxPacket.second == true)
    {
      NS_LOG_DEBUG ("Reception was previously terminated; dropping packet");
      m_phyRxDropTrace (m_currentRxPacket.first);
    }
  else if (m_errorModel != 0)
    {
      double sinr = psdHelper.TotalAvgPower (*m_rxPsd) / psdHelper.TotalAvgPower (*m_noise);
      Ptr<Packet> p = m_currentRxPacket.first;
      // Simplified calculation; the chunk is the whole packet
      double per = 1.0 - m_errorModel->GetChunkSuccessRate (sinr, p->GetSize () * 8);
      if (m_random.GetValue () > per)
        {
          NS_LOG_DEBUG ("Reception success for packet: per " << per << " sinr " << sinr);
          m_phyRxEndTrace (p, sinr);
          if (!m_pdDataIndicationCallback.IsNull ())
            {
              m_pdDataIndicationCallback (p->GetSize (), p, (uint32_t)sinr);
            }
        }
      else
        {
          /* Failure */
          NS_LOG_DEBUG ("Reception failure for packet per" << per << " sinr " << sinr);
          m_phyRxEndTrace (p, sinr);
          m_phyRxDropTrace (p);
        }
    }
  else
    {
      NS_LOG_WARN ("Missing ErrorModel");
      Ptr<Packet> p = m_currentRxPacket.first;
      m_phyRxEndTrace (p, 0);
      if (!m_pdDataIndicationCallback.IsNull ())
        {
          m_pdDataIndicationCallback (p->GetSize (), p, 0);
        }
    }

  m_rxTotalPower = 0;
  m_currentRxPacket.first = 0;
  m_currentRxPacket.second = false;
  // We may be waiting to apply a pending state change
  if (m_trxStatePending != IEEE_802_15_4_PHY_IDLE)
    {
      NS_LOG_LOGIC ("Apply pending state change to " << m_trxStatePending);
      ChangeTrxState (m_trxStatePending);
      m_trxStatePending = IEEE_802_15_4_PHY_IDLE;
      if (!m_plmeSetTRXStateConfirmCallback.IsNull ())
        {
          m_plmeSetTRXStateConfirmCallback (IEEE_802_15_4_PHY_SUCCESS);
        }
    }
  else
    {
      if (m_trxState != IEEE_802_15_4_PHY_TRX_OFF)
        {
          ChangeTrxState (IEEE_802_15_4_PHY_RX_ON);
        }
    }
}

void
LrWpanPhy::PdDataRequest (const uint32_t psduLength, Ptr<Packet> p)
{
  NS_LOG_FUNCTION (this << psduLength << p);

  if (psduLength > aMaxPhyPacketSize)
    {
      if (!m_pdDataConfirmCallback.IsNull ())
        {
          m_pdDataConfirmCallback (IEEE_802_15_4_PHY_UNSPECIFIED);
        }
      NS_LOG_DEBUG ("Drop packet because psduLength too long: " << psduLength);
      return;
    }

  if (m_trxState == IEEE_802_15_4_PHY_TX_ON)
    {
      //send down
      NS_ASSERT (m_channel);

      Ptr<LrWpanSpectrumSignalParameters> txParams = Create<LrWpanSpectrumSignalParameters> ();
      txParams->duration = CalculateTxTime (p);
      txParams->txPhy = GetObject<SpectrumPhy> ();
      txParams->psd = m_txPsd;
      txParams->txAntenna = m_antenna;
      Ptr<PacketBurst> pb = CreateObject<PacketBurst> ();
      pb->AddPacket (p);
      txParams->packetBurst = pb;
      m_channel->StartTx (txParams);
      m_pdDataRequest = Simulator::Schedule (txParams->duration, &LrWpanPhy::EndTx, this);
      ChangeTrxState (IEEE_802_15_4_PHY_BUSY_TX);
      m_phyTxBeginTrace (p);
      m_currentTxPacket.first = p;
      m_currentTxPacket.second = false;
      return;
    }
  else if ((m_trxState == IEEE_802_15_4_PHY_RX_ON)
           || (m_trxState == IEEE_802_15_4_PHY_TRX_OFF)
           || (m_trxState == IEEE_802_15_4_PHY_BUSY_TX) )
    {
      if (!m_pdDataConfirmCallback.IsNull ())
        {
          m_pdDataConfirmCallback (m_trxState);
        }
      // Drop packet, hit PhyTxDrop trace
      m_phyTxDropTrace (p);
      return;
    }
  else
    {
      NS_FATAL_ERROR ("This should be unreachable, or else state " << m_trxState << " should be added as a case");
    }
}

void
LrWpanPhy::PlmeCcaRequest (void)
{
  NS_LOG_FUNCTION (this);
  if (m_trxState == IEEE_802_15_4_PHY_RX_ON)
    {
      //call StartRx or ED and then get updated m_rxTotalPower in EndCCA?
      Time ccaTime = Seconds (8.0 / GetDataOrSymbolRate (false));
      Simulator::Schedule (ccaTime, &LrWpanPhy::EndCca, this);
    }
  else
    {
      if (!m_plmeCcaConfirmCallback.IsNull ())
        {
          m_plmeCcaConfirmCallback (m_trxState);
        }
    }
}

void
LrWpanPhy::PlmeEdRequest (void)
{
  NS_LOG_FUNCTION (this);
  if (m_trxState == IEEE_802_15_4_PHY_RX_ON)
    {
      m_rxEdPeakPower = m_rxTotalPower;
      Time edTime = Seconds (8.0 / GetDataOrSymbolRate (false));
      m_edRequest = Simulator::Schedule (edTime, &LrWpanPhy::EndEd, this);
    }
  else
    {
      if (!m_plmeEdConfirmCallback.IsNull ())
        {
          m_plmeEdConfirmCallback (m_trxState,0);
        }
    }
}

void
LrWpanPhy::PlmeGetAttributeRequest (LrWpanPibAttributeIdentifier id)
{
  NS_LOG_FUNCTION (this << id);
  LrWpanPhyEnumeration status;

  switch (id)
    {
    case phyCurrentChannel:
    case phyChannelsSupported:
    case phyTransmitPower:
    case phyCCAMode:
    case phyCurrentPage:
    case phyMaxFrameDuration:
    case phySHRDuration:
    case phySymbolsPerOctet:
      {
        status = IEEE_802_15_4_PHY_SUCCESS;
        break;
      }
    default:
      {
        status = IEEE_802_15_4_PHY_UNSUPPORTED_ATTRIBUTE;
        break;
      }
    }
  if (!m_plmeGetAttributeConfirmCallback.IsNull ())
    {
      LrWpanPhyPIBAttributes retValue;
      memcpy (&retValue, &m_phyPIBAttributes, sizeof(LrWpanPhyPIBAttributes));
      m_plmeGetAttributeConfirmCallback (status,id,&retValue);
    }
}

// Section 6.2.2.7.3
void
LrWpanPhy::PlmeSetTRXStateRequest (LrWpanPhyEnumeration state)
{
  NS_LOG_FUNCTION (this << state);

  // Check valid states (Table 14)
  NS_ABORT_IF ( (state != IEEE_802_15_4_PHY_RX_ON)
                && (state != IEEE_802_15_4_PHY_TRX_OFF)
                && (state != IEEE_802_15_4_PHY_FORCE_TRX_OFF)
                && (state != IEEE_802_15_4_PHY_TX_ON) );

  NS_LOG_LOGIC ("Trying to set m_trxState from " << m_trxState << " to " << state);

  // this method always overrides previous state setting attempts
  if (m_trxStatePending != IEEE_802_15_4_PHY_IDLE)
    {
      m_trxStatePending = IEEE_802_15_4_PHY_IDLE;
    }
  if (m_setTRXState.IsRunning ())
    {
      NS_LOG_DEBUG ("Cancel m_setTRXState");
      m_setTRXState.Cancel ();
    }

  if (state == m_trxState)
    {
      if (!m_plmeSetTRXStateConfirmCallback.IsNull ())
        {
          m_plmeSetTRXStateConfirmCallback (state);
        }
      return;
    }

  if ( ((state == IEEE_802_15_4_PHY_RX_ON)
        || (state == IEEE_802_15_4_PHY_TRX_OFF))
       && (m_trxState == IEEE_802_15_4_PHY_BUSY_TX) )
    {
      NS_LOG_DEBUG ("Phy is busy; setting state pending to " << state);
      m_trxStatePending = state;
      return;  // Send PlmeSetTRXStateConfirm later
    }

  // specification talks about being in RX_ON and having received
  // a valid SFD.  Here, we are not modelling at that level of
  // granularity, so we just test for BUSY_RX state (any part of
  // a packet being actively received)
  if ((state == IEEE_802_15_4_PHY_TRX_OFF)
      && (m_trxState == IEEE_802_15_4_PHY_BUSY_RX)
      && (m_currentRxPacket.first) && (!m_currentRxPacket.second))
    {
      NS_LOG_DEBUG ("Receiver has valid SFD; defer state change");
      m_trxStatePending = state;
      return;  // Send PlmeSetTRXStateConfirm later
    }

  if (state == IEEE_802_15_4_PHY_TX_ON)
    {
      NS_LOG_DEBUG ("turn on PHY_TX_ON");
      ChangeTrxState (IEEE_802_15_4_PHY_TX_ON);
      if ((m_trxState == IEEE_802_15_4_PHY_BUSY_RX) || (m_trxState == IEEE_802_15_4_PHY_RX_ON))
        {
          if (m_currentRxPacket.first)
            {
              //terminate reception if needed
              //incomplete reception -- force packet discard
              NS_LOG_DEBUG ("force TX_ON, terminate reception");
              m_currentRxPacket.second = true;
            }
          // Delay for turnaround time
          Time setTime = Seconds ( (double) aTurnaroundTime / GetDataOrSymbolRate (false));
          m_setTRXState = Simulator::Schedule (setTime, &LrWpanPhy::EndSetTRXState, this);
          return;
        }
      else
        {
          if (!m_plmeSetTRXStateConfirmCallback.IsNull ())
            {
              m_plmeSetTRXStateConfirmCallback (IEEE_802_15_4_PHY_SUCCESS);
            }
          return;
        }
    }

  if (state == IEEE_802_15_4_PHY_FORCE_TRX_OFF)
    {
      if (m_trxState == IEEE_802_15_4_PHY_TRX_OFF)
        {
          NS_LOG_DEBUG ("force TRX_OFF, was already off");
        }
      else
        {
          NS_LOG_DEBUG ("force TRX_OFF, SUCCESS");
          if (m_currentRxPacket.first)
            {   //terminate reception if needed
                //incomplete reception -- force packet discard
              NS_LOG_DEBUG ("force TRX_OFF, terminate reception");
              m_currentRxPacket.second = true;
            }
          if (m_trxState == IEEE_802_15_4_PHY_BUSY_TX)
            {
              NS_LOG_DEBUG ("force TRX_OFF, terminate transmission");
              m_currentTxPacket.second = true;
              m_currentTxPacket.first = 0;
            }
          ChangeTrxState (IEEE_802_15_4_PHY_TRX_OFF);
          // Clear any other state
          m_trxStatePending = IEEE_802_15_4_PHY_IDLE;
        }
      if (!m_plmeSetTRXStateConfirmCallback.IsNull ())
        {
          m_plmeSetTRXStateConfirmCallback (IEEE_802_15_4_PHY_SUCCESS);
        }
      return;
    }

  if ((state == IEEE_802_15_4_PHY_RX_ON) && (m_trxState == IEEE_802_15_4_PHY_TX_ON))
    {
      // Turnaround delay
      m_trxStatePending = IEEE_802_15_4_PHY_RX_ON;
      Time setTime = Seconds ( (double) aTurnaroundTime / GetDataOrSymbolRate (false));
      m_setTRXState = Simulator::Schedule (setTime, &LrWpanPhy::EndSetTRXState, this);
      return;
    }
  NS_FATAL_ERROR ("Catch other transitions");
}

bool
LrWpanPhy::ChannelSupported (uint8_t channel)
{
  NS_LOG_FUNCTION (this << channel);
  bool retValue = false;

  for (uint32_t i = 0; i < 32; i++)
    {
      if ((m_phyPIBAttributes.phyChannelsSupported[i] & (1 << channel)) != 0)
        {
          retValue = true;
          break;
        }
    }

  return retValue;
}

void
LrWpanPhy::PlmeSetAttributeRequest (LrWpanPibAttributeIdentifier id,
                                    LrWpanPhyPIBAttributes* attribute)
{
  NS_ASSERT (attribute);
  NS_LOG_FUNCTION (this << id << attribute);
  LrWpanPhyEnumeration status = IEEE_802_15_4_PHY_SUCCESS;

  switch (id)
    {
    case phyCurrentChannel:
      {
        if (!ChannelSupported (attribute->phyCurrentChannel))
          {
            status = IEEE_802_15_4_PHY_INVALID_PARAMETER;
          }
        if (m_phyPIBAttributes.phyCurrentChannel != attribute->phyCurrentChannel)
          {    //any packet in transmission or reception will be corrupted
            if (m_currentRxPacket.first)
              {
                m_currentRxPacket.second = true;
              }
            if (PhyIsBusy ())
              {
                m_currentTxPacket.second = true;
                m_pdDataRequest.Cancel ();
                m_currentTxPacket.first = 0;
                if (!m_pdDataConfirmCallback.IsNull ())
                  {
                    m_pdDataConfirmCallback (IEEE_802_15_4_PHY_TRX_OFF);
                  }

                if (m_trxStatePending != IEEE_802_15_4_PHY_IDLE)
                  {
                    m_trxStatePending = IEEE_802_15_4_PHY_IDLE;
                  }
              }
            m_phyPIBAttributes.phyCurrentChannel = attribute->phyCurrentChannel;
          }
        break;
      }
    case phyChannelsSupported:
      {   // only the first element is considered in the array
        if ((attribute->phyChannelsSupported[0] & 0xf8000000) != 0)
          {    //5 MSBs reserved
            status = IEEE_802_15_4_PHY_INVALID_PARAMETER;
          }
        else
          {
            m_phyPIBAttributes.phyChannelsSupported[0] = attribute->phyChannelsSupported[0];
          }
        break;
      }
    case phyTransmitPower:
      {
        if (attribute->phyTransmitPower > 0xbf)
          {
            status = IEEE_802_15_4_PHY_INVALID_PARAMETER;
          }
        else
          {
            m_phyPIBAttributes.phyTransmitPower = attribute->phyTransmitPower;
            LrWpanSpectrumValueHelper psdHelper;
            m_txPsd = psdHelper.CreateTxPowerSpectralDensity (m_phyPIBAttributes.phyTransmitPower, m_phyPIBAttributes.phyCurrentChannel);
          }
        break;
      }
    case phyCCAMode:
      {
        if ((attribute->phyCCAMode < 1) || (attribute->phyCCAMode > 3))
          {
            status = IEEE_802_15_4_PHY_INVALID_PARAMETER;
          }
        else
          {
            m_phyPIBAttributes.phyCCAMode = attribute->phyCCAMode;
          }
        break;
      }
    default:
      {
        status = IEEE_802_15_4_PHY_UNSUPPORTED_ATTRIBUTE;
        break;
      }
    }

  if (!m_plmeSetAttributeConfirmCallback.IsNull ())
    {
      m_plmeSetAttributeConfirmCallback (status, id);
    }
}

void
LrWpanPhy::SetPdDataIndicationCallback (PdDataIndicationCallback c)
{
  NS_LOG_FUNCTION (this);
  m_pdDataIndicationCallback = c;
}

void
LrWpanPhy::SetPdDataConfirmCallback (PdDataConfirmCallback c)
{
  NS_LOG_FUNCTION (this);
  m_pdDataConfirmCallback = c;
}

void
LrWpanPhy::SetPlmeCcaConfirmCallback (PlmeCcaConfirmCallback c)
{
  NS_LOG_FUNCTION (this);
  m_plmeCcaConfirmCallback = c;
}

void
LrWpanPhy::SetPlmeEdConfirmCallback (PlmeEdConfirmCallback c)
{
  NS_LOG_FUNCTION (this);
  m_plmeEdConfirmCallback = c;
}

void
LrWpanPhy::SetPlmeGetAttributeConfirmCallback (PlmeGetAttributeConfirmCallback c)
{
  NS_LOG_FUNCTION (this);
  m_plmeGetAttributeConfirmCallback = c;
}

void
LrWpanPhy::SetPlmeSetTRXStateConfirmCallback (PlmeSetTRXStateConfirmCallback c)
{
  NS_LOG_FUNCTION (this);
  m_plmeSetTRXStateConfirmCallback = c;
}

void
LrWpanPhy::SetPlmeSetAttributeConfirmCallback (PlmeSetAttributeConfirmCallback c)
{
  NS_LOG_FUNCTION (this);
  m_plmeSetAttributeConfirmCallback = c;
}

void
LrWpanPhy::ChangeTrxState (LrWpanPhyEnumeration newState)
{
  NS_LOG_LOGIC (this << " state: " << m_trxState << " -> " << newState);
  m_trxStateLogger (Simulator::Now (), m_trxState, newState);
  m_trxState = newState;
}

bool
LrWpanPhy::PhyIsBusy (void) const
{
  return ((m_trxState == IEEE_802_15_4_PHY_BUSY_TX)
          || (m_trxState == IEEE_802_15_4_PHY_BUSY_RX)
          || (m_trxState == IEEE_802_15_4_PHY_BUSY));
}

void
LrWpanPhy::EndEd ()
{
  NS_LOG_FUNCTION (this);
  uint8_t energyLevel;

  // Per IEEE802.15.4-2006 sec 6.9.7
  double ratio = m_rxEdPeakPower / m_rxSensitivity;
  ratio = 10.0 * log10 (ratio);
  if (ratio <= 10.0)
    {  // less than 10 dB
      energyLevel = 0;
    }
  else if (ratio >= 40.0)
    { // less than 40 dB
      energyLevel = 255;
    }
  else
    {
      // in-between with linear increase per sec 6.9.7
      energyLevel = (uint8_t)((ratio / 10.0 - 1.0) * (255.0 / 3.0));
    }

  if (!m_plmeEdConfirmCallback.IsNull ())
    {
      m_plmeEdConfirmCallback (IEEE_802_15_4_PHY_SUCCESS, energyLevel);
    }
}

void
LrWpanPhy::EndCca ()
{
  NS_LOG_FUNCTION (this);
  LrWpanPhyEnumeration sensedChannelState;

  if ((PhyIsBusy ()) || (m_rxTotalNum > 0))
    {
      sensedChannelState = IEEE_802_15_4_PHY_BUSY;
    }
  else if (m_phyPIBAttributes.phyCCAMode == 1)
    { //sec 6.9.9 ED detection
      // -- ED threshold at most 10 dB above receiver sensitivity.
      if (m_rxTotalPower / m_rxSensitivity >= 10.0)
        {
          sensedChannelState = IEEE_802_15_4_PHY_BUSY;
        }
      else
        {
          sensedChannelState = IEEE_802_15_4_PHY_IDLE;
        }
    }
  else if (m_phyPIBAttributes.phyCCAMode == 2)
    { //sec 6.9.9 carrier sense only
      if (m_rxTotalNum > 0)
        {
          sensedChannelState = IEEE_802_15_4_PHY_BUSY;
        }
      else
        {
          sensedChannelState = IEEE_802_15_4_PHY_IDLE;
        }
    }
  else if (m_phyPIBAttributes.phyCCAMode == 3)
    { //sect 6.9.9 both
      if ((m_rxTotalPower / m_rxSensitivity >= 10.0)
          && (m_rxTotalNum > 0))
        {
          sensedChannelState = IEEE_802_15_4_PHY_BUSY;
        }
      else
        {
          sensedChannelState = IEEE_802_15_4_PHY_IDLE;
        }
    }
  else
    {
      NS_ASSERT_MSG (false, "Invalid CCA mode");
    }

  NS_LOG_LOGIC (this << "channel sensed state: " << sensedChannelState);

  if (!m_plmeCcaConfirmCallback.IsNull ())
    {
      m_plmeCcaConfirmCallback (sensedChannelState);
    }
}

void
LrWpanPhy::EndSetTRXState ()
{
  NS_LOG_FUNCTION (this);

  NS_ABORT_IF ( (m_trxStatePending != IEEE_802_15_4_PHY_RX_ON) && (m_trxStatePending != IEEE_802_15_4_PHY_TX_ON) );
  m_trxState = m_trxStatePending;
  m_trxStatePending = IEEE_802_15_4_PHY_IDLE;

  if (!m_plmeSetTRXStateConfirmCallback.IsNull ())
    {
      m_plmeSetTRXStateConfirmCallback (m_trxState);
    }
}

void
LrWpanPhy::EndTx ()
{
  NS_LOG_FUNCTION (this);

  NS_ABORT_IF ( (m_trxState != IEEE_802_15_4_PHY_BUSY_TX) && (m_trxState != IEEE_802_15_4_PHY_TRX_OFF));

  if (m_currentTxPacket.second == false)
    {
      NS_LOG_DEBUG ("Packet successfully transmitted");
      m_phyTxEndTrace (m_currentTxPacket.first);
      if (!m_pdDataConfirmCallback.IsNull ())
        {
          m_pdDataConfirmCallback (IEEE_802_15_4_PHY_SUCCESS);
        }
    }
  else
    {
      NS_LOG_DEBUG ("Packet transmission aborted");
      m_phyTxDropTrace (m_currentTxPacket.first);
      if (!m_pdDataConfirmCallback.IsNull ())
        {
          // See if this is ever entered in another state
          NS_ASSERT (m_trxState ==  IEEE_802_15_4_PHY_TRX_OFF);
          m_pdDataConfirmCallback (m_trxState);
        }
    }
  m_currentTxPacket.first = 0;
  m_currentTxPacket.second = false;
  if (m_trxStatePending != IEEE_802_15_4_PHY_IDLE)
    {
      NS_LOG_LOGIC ("Apply pending state change to " << m_trxStatePending);
      ChangeTrxState (m_trxStatePending);
      m_trxStatePending = IEEE_802_15_4_PHY_IDLE;
      if (!m_plmeSetTRXStateConfirmCallback.IsNull ())
        {
          m_plmeSetTRXStateConfirmCallback (IEEE_802_15_4_PHY_SUCCESS);
        }
    }
  else
    {
      ChangeTrxState (IEEE_802_15_4_PHY_RX_ON);
    }
}

Time
LrWpanPhy::CalculateTxTime (Ptr<const Packet> packet)
{
  Time txTime = Seconds (0);
  bool isData = true;

  txTime = Seconds (GetPpduHeaderTxTime ()
                    + packet->GetSize () * 8.0 / GetDataOrSymbolRate (isData));

  return txTime;
}

double
LrWpanPhy::GetDataOrSymbolRate (bool isData)
{
  double rate = 0.0;

  NS_ASSERT (m_phyOption < IEEE_802_15_4_INVALID_PHY_OPTION);

  if (isData)
    {
      rate = dataSymbolRates [m_phyOption].bitRate;
    }
  else
    {
      rate = dataSymbolRates [m_phyOption].symbolRate;
    }

  return (rate * 1000.0);
}

double
LrWpanPhy::GetPpduHeaderTxTime (void)
{
  bool isData = false;
  double totalPpduHdrSymbols;

  NS_ASSERT (m_phyOption < IEEE_802_15_4_INVALID_PHY_OPTION);

  totalPpduHdrSymbols = ppduHeaderSymbolNumbers[m_phyOption].shrPreamble
    + ppduHeaderSymbolNumbers[m_phyOption].shrSfd
    + ppduHeaderSymbolNumbers[m_phyOption].phr;

  return totalPpduHdrSymbols / GetDataOrSymbolRate (isData);
}

// IEEE802.15.4-2006 Table 2 in section 6.1.2
void
LrWpanPhy::SetMyPhyOption (void)
{
  m_phyOption = IEEE_802_15_4_INVALID_PHY_OPTION;

  if (m_phyPIBAttributes.phyCurrentPage == 0)
    {
      if (m_phyPIBAttributes.phyCurrentChannel == 0)
        {  // 868 MHz BPSK
          m_phyOption = IEEE_802_15_4_868MHZ_BPSK;
        }
      else if (m_phyPIBAttributes.phyCurrentChannel <= 10)
        {  // 915 MHz BPSK
          m_phyOption = IEEE_802_15_4_915MHZ_BPSK;
        }
      else if (m_phyPIBAttributes.phyCurrentChannel <= 26)
        {  // 2.4 GHz MHz O-QPSK
          m_phyOption = IEEE_802_15_4_2_4GHZ_OQPSK;
        }
    }
  else if (m_phyPIBAttributes.phyCurrentPage == 1)
    {
      if (m_phyPIBAttributes.phyCurrentChannel == 0)
        {  // 868 MHz ASK
          m_phyOption = IEEE_802_15_4_868MHZ_ASK;
        }
      else if (m_phyPIBAttributes.phyCurrentChannel <= 10)
        {  // 915 MHz ASK
          m_phyOption = IEEE_802_15_4_915MHZ_ASK;
        }
    }
  else if (m_phyPIBAttributes.phyCurrentPage == 2)
    {
      if (m_phyPIBAttributes.phyCurrentChannel == 0)
        {  // 868 MHz O-QPSK
          m_phyOption = IEEE_802_15_4_868MHZ_OQPSK;
        }
      else if (m_phyPIBAttributes.phyCurrentChannel <= 10)
        {  // 915 MHz O-QPSK
          m_phyOption = IEEE_802_15_4_915MHZ_OQPSK;
        }
    }
}

LrWpanPhyOption
LrWpanPhy::GetMyPhyOption (void)
{
  return m_phyOption;
}

void
LrWpanPhy::SetTxPowerSpectralDensity (Ptr<SpectrumValue> txPsd)
{
  NS_LOG_FUNCTION (this << txPsd);
  NS_ASSERT (txPsd);
  m_txPsd = txPsd;
  NS_LOG_INFO ("\t computed tx_psd: " << *txPsd << "\t stored tx_psd: " << *m_txPsd);
}

void
LrWpanPhy::SetNoisePowerSpectralDensity (Ptr<const SpectrumValue> noisePsd)
{
  NS_LOG_FUNCTION (this << noisePsd);
  NS_LOG_INFO ("\t computed noise_psd: " << *noisePsd );
  NS_ASSERT (noisePsd);
  m_noise = noisePsd;
}

Ptr<const SpectrumValue>
LrWpanPhy::GetNoisePowerSpectralDensity (void)
{
  NS_LOG_FUNCTION (this);
  return m_noise;
}

void
LrWpanPhy::SetErrorModel (Ptr<LrWpanErrorModel> e)
{
  NS_LOG_FUNCTION (this << e);
  NS_ASSERT (e);
  m_errorModel = e;
}

Ptr<LrWpanErrorModel>
LrWpanPhy::GetErrorModel (void) const
{
  NS_LOG_FUNCTION (this);
  return m_errorModel;
}

} // namespace ns3
