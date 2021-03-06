######################################################################
#  architect.py - A Python Matrix Architect proxy. Performs the same
#  functions as the C++ Architect, except for creating components,
#  which are assumed to already exist, and controlling the lifetime of
#  the program.
#
#  Copyright (C) 2015 Associated Universities, Inc. Washington DC, USA.
#
#  This program is free software; you can redistribute it and/or modify
#  it under the terms of the GNU General Public License as published by
#  the Free Software Foundation; either version 2 of the License, or
#  (at your option) any later version.
#
#  This program is distributed in the hope that it will be useful, but
#  WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
#  General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with this program; if not, write to the Free Software
#  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
#
#  Correspondence concerning GBT software should be addressed as follows:
#  GBT Operations
#  National Radio Astronomy Observatory
#  P. O. Box 2
#  Green Bank, WV 24944-0002 USA
#
######################################################################

import zmq
from . import keymaster
import queue
import time
import weakref

class Architect(object):

    def __init__(self, name, km_url, context = None):
        self._keymaster = keymaster.Keymaster(km_url, context)
        self._name = name
        self._timeout = 4.0
        print('Architect created')

    def __del__(self):
        """Cleans up the Architect, and kills the keymaster cleanly."""
        del(self._keymaster)
        print('Architect terminated')

    def get_architect(self):
        """Returns a snapshot of the 'architect' section of the Keymaster data
        store.

        """
        return self._keymaster.get('architect')

    def get_components(self):
        """Returns a snapshot of the 'components' section of the Keymaster data
        store.

        """
        return self._keymaster.get('components')

    def get_connections(self):
        """Returns a snapshot of the 'connections' section of the Keymaster data
        store.

        """
        return self._keymaster.get('connections')

    def check_all_in_state(self, state):
        """check that all component states are in the state specified.

        *state*: The state to check.

        """
        comps = self.get_components()
        comp_states = {i: comps[i]['state'] for i in comps if comps[i]['active']}
        return (all(comp_states[x] == state for x in comp_states), comp_states)


    def get(self, key="Root"):
        """Callthrough to Keymaster.get(key)"""
        return self._keymaster.get(key)


    def put(self, key, value, create = False):
        """Callthrough to Keymaster.put(key, value)"""
        return self._keymaster.put(key, value, create)


    def rpc(self, key, params=[], to=5):
        """Callthrough to Keymaster.rpc(key, params, to)"""
        return self._keymaster.rpc(key, params, to)

    def rpc_function(self, prefix, to=5):
        """Callthrough to Keymaster.rpc_function(prefix). This returns an rpc
        function that can be used to access the serverl listening on
        'prefix.'

        """

        return self._keymaster.rpc_function(prefix, to)

    def wait_all_in_state(self, statename, timeout):
        """wait until component states are all in the state specified.

        *statename*: The state to wait for, a string.

        *timeout*: The length of time to wait for the state, in S (float).
        """

        rval = False
        total_wait_time = 0.0

        while True:
            rval, states = self.check_all_in_state(statename)
            if rval:
                break

            total_wait_time += 0.1
            time.sleep(0.1)

            if total_wait_time > timeout:
                break

        return (rval, states)

    def send_event(self, event):
        """Issue an arbitrary user-defined event to the FSM.

        *event*: The event.

        """
        self._keymaster.put('architect.control.command', event)
        return True

    def get_active_components(self):
        """Returns a list of components selected for this mode.
        """
        components = self.get_components()
        return [i for i in components if components[i]['active']]

    def get_system_modes(self):
        """returns a list of supported modes. Modes are specified in the
        'connections' section of the YAML configuration file.

        """

        connections = self._keymaster.get('connections')
        return list(connections.keys())

    def get_system_mode(self):
        """returns the currently set mode.
        """
        return self.get_architect()['control']['configuration']

    def set_system_mode(self, mode):
        """Set a specific mode. The mode name should be defined in the
        "connections" section of the configuration file. The system
        state *must* be 'Standby' to change the mode. If it is not,
        the function returns 'False'.

        *mode*: The mode, or "connections", key name, a string.

        """
        arch_state = self.get_state()
        modes = self.get_system_modes()

        # system must be in 'Standby' for mode changes to take place
        if arch_state != 'Standby':
            return (False,
                    "System state must be 'Standby' for this operation. It is currently '%s'"
                    % arch_state)

        # 'mode' must exist in 'connections'
        if mode not in modes:
            return (False, "Unknown mode '%s'" % mode)

        # all good, change modes
        rval = self._keymaster.put("architect.control.configuration", mode)

        if not rval:
            return (False, "Could not change mode, Keymaster error.")

        return (True, mode)

    def get_state(self):
        """
        Returns the current Architect state.
        """
        return self.get_architect()['control']['state']


    def ready(self):
        """Create DataSink connections and enter the Ready state

        """
        self.send_event('get_ready')
        return self.wait_all_in_state('Ready', self._timeout)

    def standby(self):
        """
        Close DataSink connections and enter the Standby state
        """
        self.send_event('do_standby')
        return self.wait_all_in_state('Standby', self._timeout)

    def start(self):
        """From the Ready state, prepare to run and enter the Running state.

        """
        self.send_event('start')
        return self.wait_all_in_state('Running', self._timeout)

    def stop(self):
        """
        From the Running state, shutdown and enter the Ready state.
        """
        self.send_event('stop')
        return self.wait_all_in_state('Ready', self._timeout)
