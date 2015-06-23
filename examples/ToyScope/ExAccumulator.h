// ======================================================================
// Copyright (C) 2015 Associated Universities, Inc. Washington DC, USA.
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
// General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
//
// Correspondence concerning GBT software should be addressed as follows:
//  GBT Operations
//  National Radio Astronomy Observatory
//  P. O. Box 2
//  Green Bank, WV 24944-0002 USA



#ifndef ExAccumulator_h
#define ExAccumulator_h

#include "Time.h"
#include "Component.h"
#include "DataInterface.h"
#include "DataSource.h"
#include "DataSink.h"

// An example of a really silly accumulator which down samples data
// by a moving window average. Data is output to a linux FIFO, which
// is read by another program for display.
///
/// Component Specific Keywords:
/// *  decimate - an integer controlling the number of input samples averaged per output
/// Note: the above keyword may be changed while running
///
/// Data Sinks (Inputs) - 'input_data', format is one double
/// Data Sources (Outputs): None. Accumulated data is written to a linux FIFO which is
/// then read by a simple application which plots the data.
///
class ExAccumulator : public Component
{
public:
  
    static Component *factory(std::string, std::string);
    virtual ~ExAccumulator();
    
protected:    
    ExAccumulator(std::string name, std::string km_url);

    /// Run-state loop 
    void poll();
    
    // override various base class methods
    virtual bool _do_start();
    virtual bool _do_stop();        
    
    bool connect();
    bool disconnect();
    void decimate_changed(std::string, YAML::Node);
    
protected:
    matrix::DataSink<double,matrix::select_only>     input_signal_sink;    
    FILE *fout; // for FIFO output
  
    Thread<ExAccumulator>       poll_thread;
    TCondition<bool>            poll_thread_started;
    int decimate_factor;
     
    
};

#endif


