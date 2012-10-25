// Copyright 2012 Evrytania LLC (http://www.evrytania.com)
//
// Written by James Peroulas <james@evrytania.com>
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Affero General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

#include <itpp/itbase.h>
#include <iomanip>
#include <sstream>
#include <queue>
#include <curses.h>
#include "rtl-sdr.h"
#include "common.h"
#include "capbuf.h"
#include "macros.h"

using namespace itpp;
using namespace std;

// Number of complex samples to capture.
#define CAPLENGTH 153600

typedef struct {
  vector <char> * buf;
  rtlsdr_dev_t * dev;
} callback_package_t;
static void capbuf_rtlsdr_callback(
  unsigned char * buf,
  uint32_t len,
  void * ctx
) {
  //vector <char> & capbuf_raw = *((vector <char> *)ctx);
  callback_package_t * cp=(callback_package_t *)ctx;
  vector <char> & capbuf_raw=(*((*cp).buf));
  rtlsdr_dev_t * dev=(*cp).dev;

  if (len==0) {
    cerr << "Error: received no samples from USB device..." << endl;
    ABORT(-1);
  }

  for (uint32 t=0;t<len;t++) {
    //cout << capbuf_raw.size() << endl;
    if (capbuf_raw.size()<CAPLENGTH*2) {
      capbuf_raw.push_back(buf[t]);
    }
    if (capbuf_raw.size()==CAPLENGTH*2) {
      rtlsdr_cancel_async(dev);
      break;
    }
  }
  //cout << capbuf_raw.size() << endl;
}

// This function produces a vector of captured data. The data can either
// come from live data received by the RTLSDR, or from a file containing
// previously captured data.
// Also, optionally, this function can save each set of captured data
// to a file.
double compute_fc_programmed(const double & fosc,const double & intended_flo);
void capture_data(
  // Inputs
  const double & fc_requested,
  const double & correction,
  const bool & save_cap,
  const bool & use_recorded_data,
  const string & data_dir,
  rtlsdr_dev_t * & dev,
  // Output
  cvec & capbuf,
  double & fc_programmed
) {
  // Filename used for recording or loading captured data.
  static uint32 capture_number=0;
  stringstream filename;
  filename << data_dir << "/capbuf_" << setw(4) << setfill('0') << capture_number << ".it";

  if (use_recorded_data) {
    // Read data from a file. Do not use live data.
    if (verbosity>=2) {
      cout << "Reading captured data from file: " << filename.str() << endl;
    }

    it_ifile itf(filename.str());
    itf.seek("capbuf");
    itf>>capbuf;
    itf.seek("fc");
    ivec fc_v;
    itf>>fc_v;
    if (fc_requested!=fc_v(0)) {
      cout << "Warning: while reading capture buffer " << capture_number << ", the read" << endl;
      cout << "center frequency did not match the expected center frequency." << endl;
    }
    itf.close();

  } else {
    if (verbosity>=2) {
      cout << "Capturing live data" << endl;
    }

    // Center frequency
    if (rtlsdr_set_center_freq(dev,itpp::round(fc_requested*correction))<0) {
      cerr << "Error: unable to set center frequency" << endl;
      ABORT(-1);
    }
    // Calculate the actual center frequency that was programmed.
    //fc_programmed=(double)rtlsdr_get_center_freq(dev);
    fc_programmed=compute_fc_programmed(28.8e6,fc_requested);
    //FIXME!! Only for testing!!!
    //fc_programmed=fc_requested+(fc_requested-fc_programmed);
    fc_programmed=fc_programmed+58;

    // Reset the buffer
    if (rtlsdr_reset_buffer(dev)<0) {
      cerr << "Error: unable to reset RTLSDR buffer" << endl;
      ABORT(-1);
    }

    // Read and store the data.
    // This will block until the call to rtlsdr_cancel_async().
    vector <char> capbuf_raw;
    capbuf_raw.reserve(CAPLENGTH*2);
    callback_package_t cp;
    cp.buf=&capbuf_raw;
    cp.dev=dev;
    rtlsdr_read_async(dev,capbuf_rtlsdr_callback,(void *)&cp,0,0);
    if (capbuf_raw.size()!=CAPLENGTH*2) {
      cerr << "Error: unable to read sufficient data from USB device" << endl;
      ABORT(-1);
    }

    // Convert to complex
    capbuf.set_size(CAPLENGTH);
#ifndef NDEBUG
    capbuf=NAN;
#endif
    for (uint32 t=0;t<CAPLENGTH;t++) {
      capbuf(t)=complex<double>((capbuf_raw[(t<<1)]-127.0)/128.0,(capbuf_raw[(t<<1)+1]-127.0)/128.0);
    }

  }

  // Save the capture data, if requested.
  if (save_cap) {
    if (verbosity>=2) {
      cout << "Saving captured data to file: " << filename.str() << endl;
    }
    it_file itf(filename.str(),true);
    itf << Name("capbuf") << capbuf;
    ivec fc_v(1);
    fc_v(0)=fc_requested;
    itf << Name("fc") << fc_v;
    itf.close();
  }

  capture_number++;
}

