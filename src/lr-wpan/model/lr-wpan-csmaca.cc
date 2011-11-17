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
 * Author: kwong yin <kwong-sang.yin@boeing.com>
 */

#include "ns3/simulator.h"
#include "ns3/lr-wpan-csmaca.h"
#include "ns3/lr-wpan-mac.h"
#include "ns3/log.h"

NS_LOG_COMPONENT_DEFINE ("LrWpanCsmaCa");

namespace ns3 {

NS_OBJECT_ENSURE_REGISTERED (LrWpanCsmaCa);

#ifndef MAX
#define MAX(x,y)        (((x) > (y)) ? (x) : (y))
#endif

#ifndef MIN
#define MIN(x,y)        (((x) < (y)) ? (x) : (y))
#endif

TypeId
LrWpanCsmaCa::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::LrWpanCsmaCa")
    .SetParent<Object> ()
    .AddConstructor<LrWpanCsmaCa> ()
  ;
  return tid;
}

LrWpanCsmaCa::LrWpanCsmaCa ()
{
  // TODO-- make these into ns-3 attributes

  m_isSlotted = false;
  m_macMinBE = 3;
  m_macMaxBE = 5;
  m_macMaxCSMABackoffs = 4;
  m_aUnitBackoffPeriod = 20; //20 symbols
}

LrWpanCsmaCa::~LrWpanCsmaCa ()
{
  m_mac = 0;
}

void
LrWpanCsmaCa::DoDispose ()
{
  m_lrWpanMacStateCallback = MakeNullCallback< void, LrWpanMacState> ();
  m_mac = 0;
}

void
LrWpanCsmaCa::SetMac (Ptr<LrWpanMac> mac)
{
  m_mac = mac;
}

Ptr<LrWpanMac>
LrWpanCsmaCa::GetMac (void) const
{
  return m_mac;
}

void
LrWpanCsmaCa::setSlottedCsmaCa (void)
{
  NS_LOG_FUNCTION (this);
  m_isSlotted = true;
}

void
LrWpanCsmaCa::setUnSlottedCsmaCa (void)
{
  NS_LOG_FUNCTION (this);
  m_isSlotted = false;
}

bool
LrWpanCsmaCa::isSlottedCsmaCa (void) const
{
  NS_LOG_FUNCTION (this);
  return (m_isSlotted);
}

bool
LrWpanCsmaCa::isUnSlottedCsmaCa (void) const
{
  NS_LOG_FUNCTION (this);
  return (!m_isSlotted);
}

void
LrWpanCsmaCa::setMacMinBE (uint8_t macMinBE)
{
  NS_LOG_FUNCTION (this << macMinBE);
  m_macMinBE = macMinBE;
}

uint8_t
LrWpanCsmaCa::getMacMinBE (void) const
{
  NS_LOG_FUNCTION (this);
  return m_macMinBE;
}

void
LrWpanCsmaCa::setMacMaxBE (uint8_t macMaxBE)
{
  NS_LOG_FUNCTION (this << macMaxBE);
  m_macMinBE = macMaxBE;
}

uint8_t
LrWpanCsmaCa::getMacMaxBE (void) const
{
  NS_LOG_FUNCTION (this);
  return m_macMaxBE;
}

void
LrWpanCsmaCa::setmacMaxCSMABackoffs (uint8_t macMaxCSMABackoffs)
{
  NS_LOG_FUNCTION (this << macMaxCSMABackoffs);
  m_macMaxCSMABackoffs = macMaxCSMABackoffs;
}

uint8_t
LrWpanCsmaCa::getmacMaxCSMABackoffs (void) const
{
  NS_LOG_FUNCTION (this);
  return m_macMaxCSMABackoffs;
}

void
LrWpanCsmaCa::setUnitBackoffPeriod (uint64_t unitBackoffPeriod)
{
  NS_LOG_FUNCTION (this << unitBackoffPeriod);
  m_aUnitBackoffPeriod = unitBackoffPeriod;
}

uint64_t
LrWpanCsmaCa::getUnitBackoffPeriod (void) const
{
  NS_LOG_FUNCTION (this);
  return m_aUnitBackoffPeriod;
}

//TODO:
uint64_t
LrWpanCsmaCa::getTimeToNextSlot (void) const
{
  NS_LOG_FUNCTION (this);
  uint64_t diffT = 0;


  return(diffT);

}
void
LrWpanCsmaCa::Start ()

{
  NS_LOG_FUNCTION (this);
  uint64_t backoffBoundary = 0;
  m_NB = 0;
  if (isSlottedCsmaCa ())
    {
      m_CW = 2;
      if (m_BLE)
        {
          m_BE = MIN (2, m_macMinBE);
        }
      else
        {
          m_BE = m_macMinBE;
        }
      //TODO: for slotted, locate backoff period boundary. i.e. delay to the next slot boundary
      backoffBoundary = getTimeToNextSlot ();
      Simulator::Schedule (Seconds (backoffBoundary),&LrWpanCsmaCa::RandomBackoffDelay,this);
    }
  else
    {
      m_BE = m_macMinBE;
      Simulator::ScheduleNow (&LrWpanCsmaCa::RandomBackoffDelay,this);
    }
  /*
  *  TODO: If using Backoff.cc (will need to modify Backoff::GetBackoffTime)
  *        Backoff.m_minSlots = 0;
  *        Backoff.m_ceiling = m_BE;
  *        Backoff.ResetBackoffTime(); //m_NB is same as m_numBackoffRetries in Backoff.h
  *        Backoff.m_maxRetries = macMaxCSMABackoffs;
  *        Backoff.m_slotTime = m_backoffPeriod;
  */
}

void
LrWpanCsmaCa::Cancel ()
{
}


/*
 * Delay for backoff period in the range 0 to 2^BE -1 units
 * TODO: If using Backoff.cc (Backoff::GetBackoffTime) will need to be slightly modified
 */
void
LrWpanCsmaCa::RandomBackoffDelay ()
{
  NS_LOG_FUNCTION (this);

  SeedManager::SetSeed (100);
  UniformVariable uniformVar;
  uint64_t upperBound = (uint64_t) pow (2, m_BE) - 1;
  uint64_t backoffPeriod;
  Time randomBackoff;
  uint64_t symbolRate;
  bool isData = false;


  symbolRate = (uint64_t) m_mac->GetPhy ()->GetDataOrSymbolRate (isData); //symbols per second
  uniformVar = UniformVariable (0, upperBound);
  backoffPeriod = (uint64_t)uniformVar.GetValue (); //num backoff periods
  randomBackoff = MicroSeconds (backoffPeriod * getUnitBackoffPeriod () * 1000 * 1000 / symbolRate);

  if (isUnSlottedCsmaCa ())
    {
      NS_LOG_LOGIC ("Unslotted:  requesting CCA after backoff of " << randomBackoff.GetMicroSeconds () << " us");
      Simulator::Schedule (randomBackoff,&LrWpanCsmaCa::RequestCCA,this);
    }
  else
    {
      NS_LOG_LOGIC ("Slotted:  proceeding after backoff of " << randomBackoff.GetMicroSeconds () << " us");
      Simulator::Schedule (randomBackoff,&LrWpanCsmaCa::CanProceed,this);
    }
}

// TODO : Determine if transmission can be completed before end of CAP for the slotted csmaca
//        If not delay to the next CAP
void
LrWpanCsmaCa::CanProceed ()
{
  NS_LOG_FUNCTION (this);
  uint64_t backoffBoundary = 0;
  uint8_t nextCap = 0;
  bool canProceed = true;

  if (m_BLE)
    {
    }
  else
    {
    }

  if (canProceed)
    {
      // TODO: For slotted, Perform CCA on backoff period boundary i.e. delay to next slot boundary
      backoffBoundary = getTimeToNextSlot ();
      Simulator::Schedule (Seconds (backoffBoundary),&LrWpanCsmaCa::RequestCCA,this);
    }
  else
    {
      Simulator::Schedule (Seconds (nextCap),&LrWpanCsmaCa::RandomBackoffDelay,this);
    }
}

void
LrWpanCsmaCa::RequestCCA ()
{
  NS_LOG_FUNCTION (this);
  m_mac->GetPhy ()->PlmeCcaRequest ();
}

/*
 * This function is called when the phy calls back after completing a PlmeCcaRequest
 */
void
LrWpanCsmaCa::PlmeCcaConfirm (LrWpanPhyEnumeration status)
{
  NS_LOG_FUNCTION (this << status);

  if (status == IEEE_802_15_4_PHY_IDLE)
    {
      if (isSlottedCsmaCa ())
        {
          m_CW--;
          if (m_CW == 0)
            {
              // inform MAC channel is idle
              if (!m_lrWpanMacStateCallback.IsNull ())
                {
                  NS_LOG_LOGIC ("Notifying MAC of idle channel");
                  m_lrWpanMacStateCallback (CHANNEL_IDLE);
                }
            }
          else
            {
              NS_LOG_LOGIC ("Perform CCA again, m_CW = " << m_CW);
              Simulator::ScheduleNow (&LrWpanCsmaCa::RequestCCA,this); // Perform CCA again
            }
        }
      else
        {
          // inform MAC, channel is idle
          if (!m_lrWpanMacStateCallback.IsNull ())
            {
              NS_LOG_LOGIC ("Notifying MAC of idle channel");
              m_lrWpanMacStateCallback (CHANNEL_IDLE);
            }
        }
    }
  else
    {
      if (isSlottedCsmaCa ())
        {
          m_CW = 2;
        }
      m_BE = MIN (m_BE + 1, m_macMaxBE);
      m_NB++;
      if (m_NB > m_macMaxCSMABackoffs)
        {
          // no channel found so cannot send pkt
          NS_LOG_DEBUG ("Channel access failure");
          if (!m_lrWpanMacStateCallback.IsNull ())
            {
              NS_LOG_LOGIC ("Notifying MAC of Channel access failure");
              m_lrWpanMacStateCallback (CHANNEL_ACCESS_FAILURE);
            }
          return;
        }
      else
        {
          NS_LOG_DEBUG ("Perform another backoff; m_NB = " << static_cast<uint16_t> (m_NB));
          Simulator::ScheduleNow (&LrWpanCsmaCa::RandomBackoffDelay,this); //Perform another backoff (step 2)
        }
    }
}

void
LrWpanCsmaCa::SetLrWpanMacStateCallback (LrWpanMacStateCallback c)
{
  NS_LOG_FUNCTION (this);
  m_lrWpanMacStateCallback = c;
}

} //namespace ns3
