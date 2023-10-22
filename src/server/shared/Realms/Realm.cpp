/*
 * This file is part of the AzerothCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Affero General Public License as published by the
 * Free Software Foundation; either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "Realm.h"
#include "IpAddress.h"
#include "IpNetwork.h"
#include "Log.h"
#include "Config.h"
#include <boost/asio/ip/tcp.hpp>

boost::asio::ip::tcp_endpoint Realm::GetAddressForClient(boost::asio::ip::address const& clientAddr) const
{
    boost::asio::ip::address realmIp;

    // true if Network.AnyPrivateClientIsLocal and the client's IP address is a
    // part of a RFC 1918 private network
    // https://en.wikipedia.org/wiki/Private_network#Private_IPv4_addresses
    bool clientAddrIsPrivate = sConfigMgr->GetOption<bool>("AnyPrivateClientIsLocal", false) &&
        clientAddr.is_v4() &&
        (Acore::Net::IsInNetwork(Acore::Net::make_address_v4("10.0.0.0"), Acore::Net::make_address_v4("255.0.0.0"), clientAddr.to_v4()) ||
         Acore::Net::IsInNetwork(Acore::Net::make_address_v4("172.12.0.0"), Acore::Net::make_address_v4("255.240.0.0"), clientAddr.to_v4()) ||
         Acore::Net::IsInNetwork(Acore::Net::make_address_v4("192.168.0.0"), Acore::Net::make_address_v4("255.255.0.0"), clientAddr.to_v4()));

    // true if above or the client IP is inside of the localAddress in the database
    bool clientAddrIsLocal = clientAddr.is_v4() &&
                           Acore::Net::IsInNetwork(LocalAddress->to_v4(), LocalSubnetMask->to_v4(), clientAddr.to_v4());

    // Attempt to send best address for client
    //
    // Check if client Address is loopback and if the local/external address is loopback
    if (clientAddr.is_loopback() && (LocalAddress->is_loopback() || ExternalAddress->is_loopback()))
    {
        // Try guessing if realm is also connected locally
        realmIp = clientAddr;
    }
    // Assume that user connecting from the machine that bnetserver is located on has all realms available in their local network
    else if (clientAddr.is_loopback() || clientAddrIsLocal || clientAddrIsPrivate) {
        realmIp = *LocalAddress;
    }
    else {
        realmIp = *ExternalAddress;
    }

    // Return external IP
    return boost::asio::ip::tcp_endpoint(realmIp, Port);
}
