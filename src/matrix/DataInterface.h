/*******************************************************************
 *  DataInterface.h - * These classes provide abstrations to data
 *  connections.
 *
 *  At the top level are data sources and sinks. In the simplest
 *  terms, a data sink represents one data stream (one type or named
 *  data entity). A sink of 'Foo' connects to a source of
 *  'Foo'. Sources and sinks don't know how the data gets to where it
 *  is going. Sinks send data, sources get data, how doesn't matter at
 *  this level.
 *
 *  At a lower level are the Transport classes. Transports provide the
 *  'how' mechanism to conduct data from point A (the server) to point
 *  B (the client or clients). This may be a semaphore queue (such as
 *  tsemfifo), ZMQ sockets, shared memory, real time queues, etc. One
 *  transport may also be able to serve several data sources under the
 *  hood. It is this abstraction that may be extended to provide
 *  different mechanisms to get data from A to B.
 *
 *  Copyright (C) 2015 Associated Universities, Inc. Washington DC, USA.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *  Correspondence concerning GBT software should be addressed as follows:
 *  GBT Operations
 *  National Radio Astronomy Observatory
 *  P. O. Box 2
 *  Green Bank, WV 24944-0002 USA
 *
 *******************************************************************/

#ifndef DataInterface_h
#define DataInterface_h

#include "matrix/GenericBuffer.h"
#include "matrix/DataCallback.h"
#include "matrix/TransportServer.h"
#include "matrix/TransportClient.h"
#include "matrix/DataSource.h"
#include "matrix/DataSink.h"

#endif
