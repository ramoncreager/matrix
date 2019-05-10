######################################################################
#  keymaster.py - A Python Matrix Keymaster Client
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

import string
import yaml
import zmq
import threading
import string
import random
import inspect
import weakref
import queue
from time import sleep



def gen_random_string(rand_len=10, chars=string.ascii_uppercase +
                      string.ascii_lowercase + string.digits):
    """Generates a random sequence of characters of size 'rand_len' from
       the characters provided in 'char'

    """
    return ''.join(random.choice(chars) for _ in range(rand_len))


class Keymaster(object):

    class subscriber_task(threading.Thread):
        """Thread for subscriber services."""

        def __init__(self, keymaster):
            threading.Thread.__init__(self)
            self._keymaster = weakref.ref(keymaster)
            km_url = keymaster._km_url
            # Get the as-configured publication URLs
            pub_urls = self._keymaster().get('Keymaster.URLS.AsConfigured.Pub')
            # choose the one who's transport is the same as that of our
            # keymaster's request URL. i.e., if tcp, then use the pub tcp, etc.
            used_transport = (km_url.split(b':')[0]).decode("utf-8")
            self._pub_url = \
                [p for p in pub_urls if p.split(':')[0] == used_transport][0]
            # main thread communicates to this thread via ZMQ inproc REQ/REP.
            self.pipe_url = "inproc://" + gen_random_string(20)
            self._callbacks = {}
            self.end_thread = False

        def __del__(self):
            self.end_thread = True
            self.join()

        def run(self):
            """The thread code.

            Runs the subscriber thread. Receives messages over a ZMQ
            pipe and a ZMQ subscriber socket for events both from the
            client and the KeymasterServer.

            """
            try:
                ctx = self._keymaster()._ctx
                sub_sock = ctx.socket(zmq.SUB)
                pipe = ctx.socket(zmq.REP)
                poller = zmq.Poller()
                sub_sock.connect(self._pub_url)
                pipe.bind(self.pipe_url)
                poller.register(sub_sock, flags=zmq.POLLIN)
                poller.register(pipe, flags=zmq.POLLIN)

                while not self.end_thread:
                    event = poller.poll()

                    for e in event:
                        sock = e[0]  # e[1] always POLLIN

                        if sock == pipe:
                            msg = pipe.recv_pyobj()

                            if msg == self._keymaster().SUBSCRIBE:
                                key = bytes(pipe.recv_pyobj(), 'utf-8')

                                if key == b"":
                                    key = b'Root'

                                sub_sock.setsockopt(zmq.SUBSCRIBE, key)
                                pipe.send_pyobj(True, zmq.SNDMORE)
                                pipe.send_pyobj(key)

                            elif msg == self._keymaster().UNSUBSCRIBE:
                                key = bytes(pipe.recv_pyobj(), 'utf-8')

                                if key in self._callbacks:
                                    sub_sock.setsockopt(zmq.UNSUBSCRIBE, key)
                                    self._callbacks.pop(key)
                                    pipe.send_pyobj(True, zmq.SNDMORE)
                                    pipe.send_pyobj("'%s' unsubscribed." % key)
                                else:
                                    pipe.send_pyobj(False, zmq.SNDMORE)
                                    pipe.send_pyobj(
                                        "'%s': No such key is subscribed." % key)

                            elif msg == self._keymaster().UNSUBSCRIBE_ALL:
                                keys_cleared = list(self._callbacks.keys())

                                for key in self._callbacks:
                                    sub_sock.setsockopt(zmq.UNSUBSCRIBE, key)

                                self._callbacks.clear()
                                pipe.send_pyobj(True, zmq.SNDMORE)
                                pipe.send_pyobj('Keys cleared: %s' %
                                                ', '.join(keys_cleared))

                            elif msg == self._keymaster().QUIT:
                                pipe.send_pyobj(True)
                                self.end_thread = True

                            elif msg == self._keymaster().PING:
                                pipe.send_pyobj(True)

                        if sock == sub_sock:
                            msg = sub_sock.recv_multipart()
                            key = msg[0]

                            if len(msg) > 1 and key in self._callbacks:
                                n = yaml.load(msg[1], Loader=yaml.FullLoader)
                                mci = self._callbacks[key]
                                mci(key, n)
                pipe.close()
                sub_sock.close()

            except zmq.core.error.ZMQError as e:
                print(
                    'In subscriber thread, exception zmq.core.error.ZMQError:',
                      e)
                print("pipe_url:", self.pipe_url)
                print(" pub_url:", self._pub_url)
            except:
                self.end_thread = True
            finally:
                print("Keymaster: Ending subscriber thread.")

    def __init__(self, url, ctx=None):
        self.SUBSCRIBE = 10
        self.UNSUBSCRIBE = 11
        self.UNSUBSCRIBE_ALL = 12
        self.QUIT = 13
        self.PING = 14
        self._km_url = url
        self._km = None
        self._sub_task = None

        if ctx:
            self._ctx = ctx
        else:
            self._ctx = zmq.Context(1)

        print("Keymaster created.")

    def __del__(self):
        """Cleans up the Keymaster."""
        if self._km:
            self._km.close()

        self.unsubscribe_all();
        print("Keymaster terminated.")

    def _kill_subscriber_thread(self):
        """Terminates the subscriber thread by sending it the 'QUIT' message,
        closing the control pipe, and joining on the thread.

        """
        if self._sub_task:
            pipe = self._ctx.socket(zmq.REQ)
            pipe.connect(self._sub_task.pipe_url)
            pipe.send_pyobj(self.QUIT)
            pipe.recv_pyobj()
            pipe.close()
            self._sub_task.join()
            del self._sub_task
            self._sub_task = None

    def _keymaster_socket(self):
        """returns the keymaster socket, creating one if needed."""
        if not self._km:
            self._km = self._ctx.socket(zmq.REQ)
            self._km.connect(self._km_url)
        return self._km

    def _call_keymaster(self, cmd, key, val=None, flag=None):
        """atomically calls the keymaster."""
        km = self._keymaster_socket()
        parts = [bytes(p, 'utf-8') for p in [cmd, key, val, flag] if p]
        km.send_multipart(parts)

        if km.poll(5000):
            response = km.recv()
            return yaml.load(response, Loader=yaml.FullLoader)
        return {'key': key, 'result': False,
                'err': 'Time-out when talking to Keymaster.', 'node': {}}

    def get(self, key="Root"):
        """Fetches a yaml node at 'key' from the Keymaster"""
        reply = self._call_keymaster('GET', key)

        if reply['result']:
            return reply['node']
        else:
            return {}

    def put(self, key, value, create=False):
        """Puts a value at 'key' on the Keymaster"""
        return self._call_keymaster('PUT', key, yaml.dump(value),
                                    "create" if create else "")['result']

    def delete(self, key):
        """Deletes a key from the Keymaster"""
        return self._call_keymaster('DEL', key)

    def subscribe(self, key, cb_fun):
        """Subscribes to a key on the Keymaster

        *key:*
          the key of interest. Must be a full path, i.e. 'foo.bar.baz'

        *cb_fun:*
          the callback function, which must take 2 args: the key, and a yaml node

        returns 'True' if the subscription was successful, 'False'
        otherwise. The function will fail if 'key' is already
        subscribed.

        """
        # check to see if key exists
        if not self.get(key):
            return (False, "'%s' does not exist on the Keymaster." % key)

        try:
            x = inspect.getargspec(cb_fun)

            # Should be a function that takes two parameters.
            if len(x.args) != 2:
                return (False, 'Callback function must take 2 arguments')
        except TypeError:
            # not a function at all!
            return (False, 'Callback object is not a function!')

        # start the subscriber task if not already running
        if not self._sub_task:
            self._sub_task = Keymaster.subscriber_task(self)
            self._sub_task.start()
            sleep(1)  # give it time to start

        # there is already a callback, fail.
        if key in self._sub_task._callbacks:
            return (False, "'%s' is already registered for a callback." % key)

        # everything is good, set up the callback
        pipe = self._ctx.socket(zmq.REQ)
        pipe.connect(self._sub_task.pipe_url)
        pipe.send_pyobj(self.SUBSCRIBE, zmq.SNDMORE)
        pipe.send_pyobj(key)

        rval = pipe.recv_pyobj()
        msg = pipe.recv_pyobj()
        # use same key as subscriber; python 3 str/bytes issue. In
        # this case the subscribe thread will convert a str to bytes
        # so we need to store the callback using the (possibly)
        # converted key.
        self._sub_task._callbacks[msg] = cb_fun
        return (rval, msg)

    def unsubscribe(self, key):
        """Unsubscribes from a key on the Keymaster

        *key:*
          the key of interest. Must be a full path, i.e. 'foo.bar.baz'

        returns 'True' if the key was unsubscribed, 'False' if
        not. The function will fail if the key was not previously
        subscribed.

        """

        if self._sub_task:
            pipe = self._ctx.socket(zmq.REQ)
            pipe.connect(self._sub_task.pipe_url)
            pipe.send_pyobj(self.UNSUBSCRIBE, zmq.SNDMORE)
            pipe.send_pyobj(key)
            rval = pipe.recv_pyobj()
            msg = pipe.recv_pyobj()
            return (rval, msg)
        return (False, 'No subscriber thread running!')

    def unsubscribe_all(self):
        """Causes all callbacks to be unsubscribed, and terminates the
           subscriber thread. Next call to 'subscribe' will restart
           it.

        """
        if self._sub_task:
            pipe = self._ctx.socket(zmq.REQ)
            pipe.connect(self._sub_task.pipe_url)
            pipe.send_pyobj(self.UNSUBSCRIBE_ALL)
            rval = pipe.recv_pyobj()
            msg = pipe.recv_pyobj()
            self._kill_subscriber_thread()
            return (rval, msg)
        return (False, 'No subscriber thread running!')


    def rpc(self, key, params, to=5):
        send_key = key + ".request"
        reply_key = key + ".reply"
        reply = None
        qu = queue.Queue(2)

        def rpc_cb(key, val):
            qu.put((key, val))

        if self.subscribe(reply_key, rpc_cb):
            if self.put(send_key, params):
                try:
                    reply = qu.get(timeout=to)[1]
                except queue.Empty as e:
                    print("Timed out waiting for reply.")
            else:
                print("failed to send", params, "to", send_key)

            if not self.unsubscribe(reply_key):
                print("Unable to unsubscribe to", reply_key)
        else:
            print("subscription to", reply_key, "failed")

        return reply

    def rpc_function(self, prefix, to=5):

        def rpc(key, *params):
            send_key = "%s.%s.request" % (prefix, key)
            reply_key = "%s.%s.reply" % (prefix, key)
            reply = None
            qu = queue.Queue(2)

            def rpc_cb(key, val):
                qu.put((key, val))

            if self.subscribe(reply_key, rpc_cb):
                if self.put(send_key, params):
                    try:
                        reply = qu.get(timeout=to)[1]
                    except queue.Empty as e:
                        print("Timed out waiting for reply.")
                else:
                    print("failed to send", params, "to", send_key)

                if not self.unsubscribe(reply_key):
                    print("Unable to unsubscribe to", reply_key)
            else:
                print("subscription to", reply_key, "failed")

            return reply

        return rpc


def my_callback(key, val):
    print(key)
    print(val)
