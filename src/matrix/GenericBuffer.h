/*******************************************************************
 *  GenericBuffer.h - Creates an arbitrary buffer to send over a
 *  transport. Can be useful for testing.
 *
 *  Copyright (C) 2015, 2019 Associated Universities, Inc. Washington
 *  DC, USA.
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

#if !defined(_GENERIC_BUFFER_H_)
#define _GENERIC_BUFFER_H_

#include <string>
#include <vector>
#include <string.h>
#include <yaml-cpp/yaml.h>

namespace matrix
{

    /**
     * \class GenericBuffer
     *
     * This provides a buffer class for which a specialization of DataSink
     * exists to send _not the class itself_ but only the contents of
     * buffer attached to it over the line. This allows a
     * DataSource<GenericBuffer> to be dynamically configured to be the
     * data source for any sink, provided the buffer is set to the right
     * size and is structured appropriately. A (contrived) example:
     *
     *      struct foo
     *      {
     *          int a;
     *          int b;
     *          double c;
     *          int d;
     *      };
     *
     *      DataSource<GenericBuffer> src;
     *      DataSink<foo> sink;
     *      GenericBuffer buf;
     *      buf.resize(sizeof(foo));
     *      foo *fp = (foo *)buf.data();
     *      fp->a = 0x1234;
     *      fp->b = 0x2345;
     *      fp->c = 3.14159;
     *      fp->d = 0x3456;
     *      src.publish(buf);
     *
     *      // if 'sink' is connected to 'src', then:
     *      foo msg;
     *      sink.get(msg);
     *      cout << hex << msg.a
     *           << ", " << msg.b
     *           << ", " << dec << msg.c
     *           << ", " << hex << msg.d << endl;
     *      // output is "1234, 2345, 3.14159, 3456"
     *
     * So why do this? Why not just use a DataSource<foo> to begin with?
     * The answer to this is that now we can _dynamically_ create a data
     * source that will satisfy any DataSink. Now it satisfies
     * DataSink<foo>, but later in the life of the program the same
     * DataSource can publish data that will satisfy a DataSink<bar> for a
     * struct bar. Such a DataSource would allow a hypothetical component
     * to dynamically at run-time read a description of 'bar' somewhere
     * (from the Keymaster, for example), resize the GenericBuffer 'buf',
     * load it according to the description, and publish a 'bar'
     * compatible payload. This can be very useful during program
     * development, where a component is being developed and tested but
     * an upstream component is not yet available. A generic test
     * component as described above may be used in its place.
     *
     */

    class GenericBuffer
    {
    public:
        GenericBuffer()
        {
        }

        GenericBuffer(const GenericBuffer &gb)
        {
            _copy(gb);
        }

        void resize(size_t size)
        {
            _buffer.resize(size);
        }

        size_t size() const
        {
            return _buffer.size();
        }

        unsigned char *data()
        {
            return _buffer.data();
        }

        const GenericBuffer &operator=(const GenericBuffer &rhs)
        {
            _copy(rhs);
            return *this;
        }

    private:
        void _copy(const GenericBuffer &gb)
        {
            if (_buffer.size() != gb._buffer.size())
            {
                _buffer.resize(gb._buffer.size());
            }

            memcpy((void *)_buffer.data(), (void *)gb._buffer.data(), _buffer.size());
        }

        std::vector<unsigned char> _buffer;
    };

    struct data_description
    {
        enum types
        {
            INT8_T,
            UINT8_T,
            INT16_T,
            UINT16_T,
            INT32_T,
            UINT32_T,
            INT64_T,
            UINT64_T,
            CHAR,
            UNSIGNED_CHAR,
            SHORT,
            UNSIGNED_SHORT,
            INT,
            UNSIGNED_INT,
            LONG,
            UNSIGNED_LONG,
            BOOL,
            FLOAT,
            DOUBLE,
            LONG_DOUBLE,
            TIME_T
        };

        struct data_field
        {
            std::string name;         // field name
            types type;               // field type
            size_t offset;            // offset into buffer
            size_t elements;          // 1 or more of these
            bool skip;                // flag to ignore field when logging
        };

        data_description();
        data_description(YAML::Node fields);
        void add_field(std::vector<std::string> &f);
        size_t size();

        double interval; // in seconds
        std::list<data_field> fields;

        static std::vector<size_t> type_info;
        static std::map<std::string, types> typenames_to_types;
    };

    template <typename T>
        T get_data_buffer_value(unsigned char *buf, size_t offset)
    {
        return *((T *)(buf + offset));
    }

    template <typename T>
        void set_data_buffer_value(unsigned char *buf, size_t offset, T val)
    {
        *((T *)(buf + offset)) = val;
    }

}

#endif
