/*******************************************************************
 *  GenericBuffer.cc - Implementeation of the GenericBuffer
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

#include "matrix/GenericBuffer.h"
#include "matrix/matrix_util.h"
#include <vector>

using namespace std;

namespace matrix
{
    std::map<std::string, data_description::types> data_description::typenames_to_types =
    {
        {"int8_t", data_description::INT8_T},
        {"uint8_t", data_description::UINT8_T},
        {"int16_t", data_description::INT16_T},
        {"uint16_t", data_description::UINT16_T},
        {"int32_t", data_description::INT32_T},
        {"uint32_t", data_description::UINT32_T},
        {"int64_t", data_description::INT64_T},
        {"uint64_t", data_description::UINT64_T},
        {"char", data_description::CHAR},
        {"unsigned char", data_description::UNSIGNED_CHAR},
        {"short", data_description::SHORT},
        {"unsigned short", data_description::UNSIGNED_SHORT},
        {"int", data_description::INT},
        {"unsigned int", data_description::UNSIGNED_INT},
        {"long", data_description::LONG},
        {"unsigned long", data_description::UNSIGNED_LONG},
        {"bool", data_description::BOOL},
        {"float", data_description::FLOAT},
        {"double", data_description::DOUBLE},
        {"long double", data_description::LONG_DOUBLE},
        {"Time_t", data_description::TIME_T}
    };

    std::vector<size_t> data_description::type_info =
    {
        sizeof(int8_t),
        sizeof(uint8_t),
        sizeof(int16_t),
        sizeof(uint16_t),
        sizeof(int32_t),
        sizeof(uint32_t),
        sizeof(int64_t),
        sizeof(uint64_t),
        sizeof(char),
        sizeof(unsigned char),
        sizeof(short),
        sizeof(unsigned short),
        sizeof(int),
        sizeof(unsigned int),
        sizeof(long),
        sizeof(unsigned long),
        sizeof(bool),
        sizeof(float),
        sizeof(double),
        sizeof(long double),
        sizeof(long long)   // Time::Time_t
    };

    /**
     * Constructor to the data_description nested structure, which
     * contains a description of the data to be output for a source.
     *
     */

    data_description::data_description()
    {
    }

    data_description::data_description(YAML::Node fields)
    {
        if (fields.IsSequence())
        {
            vector<vector<string> > vs = fields.as<vector<vector<string> > >();

            for (vector<vector<string> >::iterator k = vs.begin(); k != vs.end(); ++k)
            {
                add_field(*k);
            }
        }
        else if (fields.IsMap())
        {
            map<string, vector<string> > vs = fields.as<map<string, vector<string> > >();

            for (size_t i = 0; i < vs.size(); ++i)
            {
                std::string s = std::to_string(i);
                auto p = vs.find(s);
                if (p != vs.end())
                {
                    add_field(vs[s]);
                }
                else
                {
                    ostringstream msg;
                    msg << "Unable to find entry " << s << " in parsing data description" << endl;
                    msg << "YAML input was: " << fields << endl;
                    throw MatrixException("data_description::data_description()",
                                          msg.str());
                }
            }
        }
        else
        {
            ostringstream msg;
            msg << "Unable to convert YAML input " << fields << "Neither vector or map.";
            throw MatrixException("TestDataGenerator::_read_source_information()",
                                  msg.str());
        }
    }

    /**
     * Adds a field description to the fields list in the data_description object. Field
     * descriptions consist of a vector<string> of the form
     *
     *      [field_name, field_type, initial_val]
     *
     * The fields list describes the data structure.
     *
     * @param f: the vector containing the description for the field.
     *
     */

    void data_description::add_field(std::vector<std::string> &f)
    {
        data_description::data_field df;

        df.name = f[0];
        df.type = typenames_to_types[f[1]];
        df.elements = mxutils::convert<size_t>(f[2]);

        if (f.size() > 3 && f[3] == "nolog")
        {
            df.skip = true;
        }
        else
        {
            df.skip = false;
        }
        fields.push_back(df);
    }

    /**
     * Computes the size of the total buffer needed for the data source,
     * and the offset of the various fields. gcc in the x86_64
     * architecture stores structures in memory as a multiple of the
     * largest type of field in the structure. Thus, if the largest
     * element is an int, the size will be a multiple of 4; if it is a
     * long, of 8; etc. All elements must be stored on boundaries that
     * respect their size: int16_t on two-byte boundaries, int on 4-byte
     * boundaries, etc. Padding is added to make this work.
     *
     * case of long or double as largest field:
     *
     *     ---------------|---------------
     *               8 - byte value
     *     ---------------|---------------
     *       4-byte val   | 4-byte val
     *     ---------------|---------------
     *       4-byte val   |  padding (pd)
     *     ---------------|---------------
     *               8 - byte value
     *     ---------------|---------------
     *     1BV|1BV|  2BV  | 4-byte val
     *     ---------------|---------------
     *     1BV|    pd     | 4-byte val
     *
     *              etc...
     *
     * case of int or float being largest field:
     *
     *     ---------------
     *      4-byte value
     *     ---------------
     *      2BV   |1BV|pd
     *     ---------------
     *      1BV|pd| 2BV
     *     ---------------
     *      4-byte value
     *     ---------------
     *      2BV   | pd
     *
     *        etc...
     *
     * Structures that contain only one field are the size of that field
     * without any padding, since that one element is the largest element
     * type. So for a struct foo_t {int16_t i16;};, sizeof(foo_t) would be
     * 2.
     *
     * As it is computing the size this function also saves the offsets
     * into the various 'data_field' structures so that the data may be
     * properly accessed later.
     *
     * @return A size_t which is the size that the GenericBuffer should be
     * set to in order to contain the data properly.
     *
     */

    size_t data_description::size()
    {
        // storage element size, offset in element, number of elements
        // used.
        size_t s_elem_size, offset(0), s_elems(1);

        // find largest element in structure.
        std::list<data_field>::iterator i =
            max_element(fields.begin(), fields.end(),
                        [this](data_field &i, data_field &j)
                        {
                            return type_info[i.type] < type_info[j.type];
                        });
        s_elem_size = type_info[i->type];

        // compute the offset and multiples of largest element
        for (list<data_field>::iterator i = fields.begin(); i != fields.end(); ++i)
        {
            size_t s(type_info[i->type]);
            offset += offset % s;

            if (s_elem_size - offset < s)
            {
                offset = 0;
                s_elems++;
            }

            i->offset = s_elem_size * (s_elems - 1) + offset;
            offset += s;
        }

        return s_elem_size * s_elems;
    }
}
