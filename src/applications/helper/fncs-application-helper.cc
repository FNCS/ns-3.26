/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2008 INRIA
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
 * Author: Mathieu Lacage <mathieu.lacage@sophia.inria.fr>
 */
#include "fncs-application-helper.h"
#include "ns3/fncs-application.h"
#include "ns3/uinteger.h"
#include "ns3/names.h"
#include "ns3/ipv4.h"

#include <sstream>
#include <string>

namespace ns3 {

FncsApplicationHelper::FncsApplicationHelper (std::string prefix, size_t offset)
{
  m_factory.SetTypeId (FncsApplication::GetTypeId ());
  m_prefix = prefix;
  m_counter = offset;
}

void 
FncsApplicationHelper::SetAttribute (
  std::string name, 
  const AttributeValue &value)
{
  m_factory.Set (name, value);
}

ApplicationContainer
FncsApplicationHelper::Install (Ptr<Node> node)
{
  return ApplicationContainer (InstallPriv (node));
}

ApplicationContainer
FncsApplicationHelper::Install (Ptr<Node> node, const std::string &name)
{
  return ApplicationContainer (InstallPriv (node, name));
}

ApplicationContainer
FncsApplicationHelper::Install (std::string nodeName)
{
  Ptr<Node> node = Names::Find<Node> (nodeName);
  return ApplicationContainer (InstallPriv (node, nodeName));
}

ApplicationContainer
FncsApplicationHelper::Install (NodeContainer c)
{
  ApplicationContainer apps;
  for (NodeContainer::Iterator i = c.Begin (); i != c.End (); ++i)
    {
      apps.Add (InstallPriv (*i));
    }

  return apps;
}

Ptr<Application>
FncsApplicationHelper::InstallPriv (Ptr<Node> node)
{
  std::string counter = (std::ostringstream() << m_counter++).str();
  return InstallPriv(node, m_prefix+counter);
}

Ptr<Application>
FncsApplicationHelper::InstallPriv (Ptr<Node> node, const std::string &name)
{
  Ptr<FncsApplication> app = m_factory.Create<FncsApplication> ();
  app->SetName(name);
  Ptr<Ipv4> net = node->GetObject<Ipv4>();
  Ipv4InterfaceAddress interface_address = net->GetAddress(1,0);
  Ipv4Address address = interface_address.GetLocal();
  app->SetLocal(address, 1234);
  node->AddApplication (app);

  return app;
}

} // namespace ns3
