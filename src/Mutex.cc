/*******************************************************************
 ** Mutex.h - Implements a Mutex class that encapsulates Windows/Posix
 *            mutexes.
 *
 *  Copyright (C) 2002 Associated Universities, Inc. Washington DC, USA.
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
 *  $Id: Mutex.cc,v 1.4 2006/09/28 18:51:30 rcreager Exp $
 *
 *******************************************************************/

#include "matrix/Mutex.h"

/****************************************************************//**
 * \class Mutex
 *
 * A simple encapsulation of Posix mutexes.
 *
 *******************************************************************/

namespace matrix
{
    Mutex::~Mutex()
    {
        pthread_mutex_destroy(&mutex);
    }

    Mutex::Mutex()
    {
        pthread_mutex_init(&mutex, 0);
    }

/****************************************************************//**
 * Locks the mutex.
 *
 * @return 0 on success, error code otherwise (see man
 * pthread_mutex_lock())
 *
 *******************************************************************/

    int Mutex::lock()
    {
        return pthread_mutex_lock(&mutex);
    }

/****************************************************************//**
 * Unlocks the mutex.
 *
 * @return 0 on success, error code otherwise (se man
 * pthread_mutex_unlock())
 *
 *******************************************************************/

    int Mutex::unlock()
    {
        return pthread_mutex_unlock(&mutex);
    }
}; // namespace matrix