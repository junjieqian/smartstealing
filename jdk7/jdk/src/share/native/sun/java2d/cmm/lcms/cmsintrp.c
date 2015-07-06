/*
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.  Oracle designates this
 * particular file as subject to the "Classpath" exception as provided
 * by Oracle in the LICENSE file that accompanied this code.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 */

// This file is available under and governed by the GNU General Public
// License version 2 only, as published by the Free Software Foundation.
// However, the following notice accompanied the original version of this
// file:
//
//---------------------------------------------------------------------------------
//
//  Little Color Management System
//  Copyright (c) 1998-2010 Marti Maria Saguer
//
// Permission is hereby granted, free of charge, to any person obtaining
// a copy of this software and associated documentation files (the "Software"),
// to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the Software
// is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
// LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
// WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
//
//---------------------------------------------------------------------------------
//

#include "lcms2_internal.h"

// This module incorporates several interpolation routines, for 1, 3, 4, 5, 6, 7 and 8 channels on input and
// up to 65535 channels on output. The user may change those by using the interpolation plug-in

// Interpolation routines by default
static cmsInterpFunction DefaultInterpolatorsFactory(cmsUInt32Number nInputChannels, cmsUInt32Number nOutputChannels, cmsUInt32Number dwFlags);

// This is the default factory
static cmsInterpFnFactory Interpolators = DefaultInterpolatorsFactory;


// Main plug-in entry
cmsBool  _cmsRegisterInterpPlugin(cmsPluginBase* Data)
{
    cmsPluginInterpolation* Plugin = (cmsPluginInterpolation*) Data;

    if (Data == NULL) {

        Interpolators = DefaultInterpolatorsFactory;
        return TRUE;
    }

    // Set replacement functions
    Interpolators = Plugin ->InterpolatorsFactory;
    return TRUE;
}


// Set the interpolation method
static
cmsBool _cmsSetInterpolationRoutine(cmsInterpParams* p)
{
    // Invoke factory, possibly in the Plug-in
    p ->Interpolation = Interpolators(p -> nInputs, p ->nOutputs, p ->dwFlags);

    // If unsupported by the plug-in, go for the LittleCMS default.
    // If happens only if an extern plug-in is being used
    if (p ->Interpolation.Lerp16 == NULL)
        p ->Interpolation = DefaultInterpolatorsFactory(p ->nInputs, p ->nOutputs, p ->dwFlags);

    // Check for valid interpolator (we just check one member of the union)
    if (p ->Interpolation.Lerp16 == NULL) {
            return FALSE;
    }
    return TRUE;
}


// This function precalculates as many parameters as possible to speed up the interpolation.
cmsInterpParams* _cmsComputeInterpParamsEx(cmsContext ContextID,
                                           const cmsUInt32Number nSamples[],
                                           int InputChan, int OutputChan,
                                           const void *Table,
                                           cmsUInt32Number dwFlags)
{
    cmsInterpParams* p;
    int i;

    // Check for maximum inputs
    if (InputChan > MAX_INPUT_DIMENSIONS) {
             cmsSignalError(ContextID, cmsERROR_RANGE, "Too many input channels (%d channels, max=%d)", InputChan, MAX_INPUT_DIMENSIONS);
            return NULL;
    }

    // Creates an empty object
    p = (cmsInterpParams*) _cmsMallocZero(ContextID, sizeof(cmsInterpParams));
    if (p == NULL) return NULL;

    // Keep original parameters
    p -> dwFlags  = dwFlags;
    p -> nInputs  = InputChan;
    p -> nOutputs = OutputChan;
    p ->Table     = Table;
    p ->ContextID  = ContextID;

    // Fill samples per input direction and domain (which is number of nodes minus one)
    for (i=0; i < InputChan; i++) {

        p -> nSamples[i] = nSamples[i];
        p -> Domain[i]   = nSamples[i] - 1;
    }

    // Compute factors to apply to each component to index the grid array
    p -> opta[0] = p -> nOutputs;
    for (i=1; i < InputChan; i++)
        p ->opta[i] = p ->opta[i-1] * nSamples[InputChan-i];


    if (!_cmsSetInterpolationRoutine(p)) {
         cmsSignalError(ContextID, cmsERROR_UNKNOWN_EXTENSION, "Unsupported interpolation (%d->%d channels)", InputChan, OutputChan);
        _cmsFree(ContextID, p);
        return NULL;
    }

    // All seems ok
    return p;
}


// This one is a wrapper on the anterior, but assuming all directions have same number of nodes
cmsInterpParams* _cmsComputeInterpParams(cmsContext ContextID, int nSamples, int InputChan, int OutputChan, const void* Table, cmsUInt32Number dwFlags)
{
    int i;
    cmsUInt32Number Samples[MAX_INPUT_DIMENSIONS];

    // Fill the auxiliar array
    for (i=0; i < MAX_INPUT_DIMENSIONS; i++)
        Samples[i] = nSamples;

    // Call the extended function
    return _cmsComputeInterpParamsEx(ContextID, Samples, InputChan, OutputChan, Table, dwFlags);
}


// Free all associated memory
void _cmsFreeInterpParams(cmsInterpParams* p)
{
    if (p != NULL) _cmsFree(p ->ContextID, p);
}


// Inline fixed point interpolation
cmsINLINE cmsUInt16Number LinearInterp(cmsS15Fixed16Number a, cmsS15Fixed16Number l, cmsS15Fixed16Number h)
{
    cmsUInt32Number dif = (cmsUInt32Number) (h - l) * a + 0x8000;
    dif = (dif >> 16) + l;
    return (cmsUInt16Number) (dif);
}


//  Linear interpolation (Fixed-point optimized)
static
void LinLerp1D(register const cmsUInt16Number Value[],
               register cmsUInt16Number Output[],
               register const cmsInterpParams* p)
{
    cmsUInt16Number y1, y0;
    int cell0, rest;
    int val3;
    const cmsUInt16Number* LutTable = (cmsUInt16Number*) p ->Table;

    // if last value...
    if (Value[0] == 0xffff) {

        Output[0] = LutTable[p -> Domain[0]];
        return;
    }

    val3 = p -> Domain[0] * Value[0];
    val3 = _cmsToFixedDomain(val3);    // To fixed 15.16

    cell0 = FIXED_TO_INT(val3);             // Cell is 16 MSB bits
    rest  = FIXED_REST_TO_INT(val3);        // Rest is 16 LSB bits

    y0 = LutTable[cell0];
    y1 = LutTable[cell0+1];


    Output[0] = LinearInterp(rest, y0, y1);
}


// Floating-point version of 1D interpolation
static
void LinLerp1Dfloat(const cmsFloat32Number Value[],
                    cmsFloat32Number Output[],
                    const cmsInterpParams* p)
{
       cmsFloat32Number y1, y0;
       cmsFloat32Number val2, rest;
       int cell0, cell1;
       const cmsFloat32Number* LutTable = (cmsFloat32Number*) p ->Table;

       // if last value...
       if (Value[0] == 1.0) {
           Output[0] = LutTable[p -> Domain[0]];
           return;
       }

       val2 = p -> Domain[0] * Value[0];

       cell0 = (int) floor(val2);
       cell1 = (int) ceil(val2);

       // Rest is 16 LSB bits
       rest = val2 - cell0;

       y0 = LutTable[cell0] ;
       y1 = LutTable[cell1] ;

       Output[0] = y0 + (y1 - y0) * rest;
}



// Eval gray LUT having only one input channel
static
void Eval1Input(register const cmsUInt16Number Input[],
                register cmsUInt16Number Output[],
                register const cmsInterpParams* p16)
{
       cmsS15Fixed16Number fk;
       cmsS15Fixed16Number k0, k1, rk, K0, K1;
       int v;
       cmsUInt32Number OutChan;
       const cmsUInt16Number* LutTable = (cmsUInt16Number*) p16 -> Table;

       v = Input[0] * p16 -> Domain[0];
       fk = _cmsToFixedDomain(v);

       k0 = FIXED_TO_INT(fk);
       rk = (cmsUInt16Number) FIXED_REST_TO_INT(fk);

       k1 = k0 + (Input[0] != 0xFFFFU ? 1 : 0);

       K0 = p16 -> opta[0] * k0;
       K1 = p16 -> opta[0] * k1;

       for (OutChan=0; OutChan < p16->nOutputs; OutChan++) {

           Output[OutChan] = LinearInterp(rk, LutTable[K0+OutChan], LutTable[K1+OutChan]);
       }
}



// Eval gray LUT having only one input channel
static
void Eval1InputFloat(const cmsFloat32Number Value[],
                     cmsFloat32Number Output[],
                     const cmsInterpParams* p)
{
    cmsFloat32Number y1, y0;
    cmsFloat32Number val2, rest;
    int cell0, cell1;
    cmsUInt32Number OutChan;
    const cmsFloat32Number* LutTable = (cmsFloat32Number*) p ->Table;

        // if last value...
       if (Value[0] == 1.0) {
           Output[0] = LutTable[p -> Domain[0]];
           return;
       }

       val2 = p -> Domain[0] * Value[0];

       cell0 = (int) floor(val2);
       cell1 = (int) ceil(val2);

       // Rest is 16 LSB bits
       rest = val2 - cell0;

       cell0 *= p -> opta[0];
       cell1 *= p -> opta[0];

       for (OutChan=0; OutChan < p->nOutputs; OutChan++) {

            y0 = LutTable[cell0 + OutChan] ;
            y1 = LutTable[cell1 + OutChan] ;

            Output[OutChan] = y0 + (y1 - y0) * rest;
       }
}



// Trilinear interpolation (16 bits) - cmsFloat32Number version
static
void TrilinearInterpFloat(const cmsFloat32Number Input[],
                          cmsFloat32Number Output[],
                          const cmsInterpParams* p)

{
#   define LERP(a,l,h)      (cmsFloat32Number) ((l)+(((h)-(l))*(a)))
#   define DENS(i,j,k)      (LutTable[(i)+(j)+(k)+OutChan])

    const cmsFloat32Number* LutTable = (cmsFloat32Number*) p ->Table;
    cmsFloat32Number      px, py, pz;
    int        x0, y0, z0,
               X0, Y0, Z0, X1, Y1, Z1;
    int        TotalOut, OutChan;
    cmsFloat32Number      fx, fy, fz,
        d000, d001, d010, d011,
        d100, d101, d110, d111,
        dx00, dx01, dx10, dx11,
        dxy0, dxy1, dxyz;

    TotalOut   = p -> nOutputs;

    px = Input[0] * p->Domain[0];
    py = Input[1] * p->Domain[1];
    pz = Input[2] * p->Domain[2];

    x0 = (int) _cmsQuickFloor(px); fx = px - (cmsFloat32Number) x0;
    y0 = (int) _cmsQuickFloor(py); fy = py - (cmsFloat32Number) y0;
    z0 = (int) _cmsQuickFloor(pz); fz = pz - (cmsFloat32Number) z0;

    X0 = p -> opta[2] * x0;
    X1 = X0 + (Input[0] >= 1.0 ? 0 : p->opta[2]);

    Y0 = p -> opta[1] * y0;
    Y1 = Y0 + (Input[1] >= 1.0 ? 0 : p->opta[1]);

    Z0 = p -> opta[0] * z0;
    Z1 = Z0 + (Input[2] >= 1.0 ? 0 : p->opta[0]);

    for (OutChan = 0; OutChan < TotalOut; OutChan++) {

        d000 = DENS(X0, Y0, Z0);
        d001 = DENS(X0, Y0, Z1);
        d010 = DENS(X0, Y1, Z0);
        d011 = DENS(X0, Y1, Z1);

        d100 = DENS(X1, Y0, Z0);
        d101 = DENS(X1, Y0, Z1);
        d110 = DENS(X1, Y1, Z0);
        d111 = DENS(X1, Y1, Z1);


        dx00 = LERP(fx, d000, d100);
        dx01 = LERP(fx, d001, d101);
        dx10 = LERP(fx, d010, d110);
        dx11 = LERP(fx, d011, d111);

        dxy0 = LERP(fy, dx00, dx10);
        dxy1 = LERP(fy, dx01, dx11);

        dxyz = LERP(fz, dxy0, dxy1);

        Output[OutChan] = dxyz;
    }


#   undef LERP
#   undef DENS
}

// Trilinear interpolation (16 bits) - optimized version
static
void TrilinearInterp16(register const cmsUInt16Number Input[],
                       register cmsUInt16Number Output[],
                       register const cmsInterpParams* p)

{
#define DENS(i,j,k) (LutTable[(i)+(j)+(k)+OutChan])
#define LERP(a,l,h)     (cmsUInt16Number) (l + ROUND_FIXED_TO_INT(((h-l)*a)))

           const cmsUInt16Number* LutTable = (cmsUInt16Number*) p ->Table;
           int        OutChan, TotalOut;
           cmsS15Fixed16Number    fx, fy, fz;
  register int        rx, ry, rz;
           int        x0, y0, z0;
  register int        X0, X1, Y0, Y1, Z0, Z1;
           int        d000, d001, d010, d011,
                      d100, d101, d110, d111,
                      dx00, dx01, dx10, dx11,
                      dxy0, dxy1, dxyz;

    TotalOut   = p -> nOutputs;

    fx = _cmsToFixedDomain((int) Input[0] * p -> Domain[0]);
    x0  = FIXED_TO_INT(fx);
    rx  = FIXED_REST_TO_INT(fx);    // Rest in 0..1.0 domain


    fy = _cmsToFixedDomain((int) Input[1] * p -> Domain[1]);
    y0  = FIXED_TO_INT(fy);
    ry  = FIXED_REST_TO_INT(fy);

    fz = _cmsToFixedDomain((int) Input[2] * p -> Domain[2]);
    z0 = FIXED_TO_INT(fz);
    rz = FIXED_REST_TO_INT(fz);


    X0 = p -> opta[2] * x0;
    X1 = X0 + (Input[0] == 0xFFFFU ? 0 : p->opta[2]);

    Y0 = p -> opta[1] * y0;
    Y1 = Y0 + (Input[1] == 0xFFFFU ? 0 : p->opta[1]);

    Z0 = p -> opta[0] * z0;
    Z1 = Z0 + (Input[2] == 0xFFFFU ? 0 : p->opta[0]);

    for (OutChan = 0; OutChan < TotalOut; OutChan++) {

        d000 = DENS(X0, Y0, Z0);
        d001 = DENS(X0, Y0, Z1);
        d010 = DENS(X0, Y1, Z0);
        d011 = DENS(X0, Y1, Z1);

        d100 = DENS(X1, Y0, Z0);
        d101 = DENS(X1, Y0, Z1);
        d110 = DENS(X1, Y1, Z0);
        d111 = DENS(X1, Y1, Z1);


        dx00 = LERP(rx, d000, d100);
        dx01 = LERP(rx, d001, d101);
        dx10 = LERP(rx, d010, d110);
        dx11 = LERP(rx, d011, d111);

        dxy0 = LERP(ry, dx00, dx10);
        dxy1 = LERP(ry, dx01, dx11);

        dxyz = LERP(rz, dxy0, dxy1);

        Output[OutChan] = (cmsUInt16Number) dxyz;
    }


#   undef LERP
#   undef DENS
}


// Tetrahedral interpolation, using Sakamoto algorithm.
#define DENS(i,j,k) (LutTable[(i)+(j)+(k)+OutChan])
static
void TetrahedralInterpFloat(const cmsFloat32Number Input[],
                            cmsFloat32Number Output[],
                            const cmsInterpParams* p)
{
    const cmsFloat32Number* LutTable = (cmsFloat32Number*) p -> Table;
    cmsFloat32Number     px, py, pz;
    int        x0, y0, z0,
               X0, Y0, Z0, X1, Y1, Z1;
    cmsFloat32Number     rx, ry, rz;
    cmsFloat32Number     c0, c1=0, c2=0, c3=0;
    int                  OutChan, TotalOut;

    TotalOut   = p -> nOutputs;

    px = Input[0] * p->Domain[0];
    py = Input[1] * p->Domain[1];
    pz = Input[2] * p->Domain[2];

    x0 = (int) _cmsQuickFloor(px); rx = (px - (cmsFloat32Number) x0);
    y0 = (int) _cmsQuickFloor(py); ry = (py - (cmsFloat32Number) y0);
    z0 = (int) _cmsQuickFloor(pz); rz = (pz - (cmsFloat32Number) z0);


    X0 = p -> opta[2] * x0;
    X1 = X0 + (Input[0] >= 1.0 ? 0 : p->opta[2]);

    Y0 = p -> opta[1] * y0;
    Y1 = Y0 + (Input[1] >= 1.0 ? 0 : p->opta[1]);

    Z0 = p -> opta[0] * z0;
    Z1 = Z0 + (Input[2] >= 1.0 ? 0 : p->opta[0]);

    for (OutChan=0; OutChan < TotalOut; OutChan++) {

       // These are the 6 Tetrahedral

        c0 = DENS(X0, Y0, Z0);

        if (rx >= ry && ry >= rz) {

            c1 = DENS(X1, Y0, Z0) - c0;
            c2 = DENS(X1, Y1, Z0) - DENS(X1, Y0, Z0);
            c3 = DENS(X1, Y1, Z1) - DENS(X1, Y1, Z0);

        }
        else
            if (rx >= rz && rz >= ry) {

                c1 = DENS(X1, Y0, Z0) - c0;
                c2 = DENS(X1, Y1, Z1) - DENS(X1, Y0, Z1);
                c3 = DENS(X1, Y0, Z1) - DENS(X1, Y0, Z0);

            }
            else
                if (rz >= rx && rx >= ry) {

                    c1 = DENS(X1, Y0, Z1) - DENS(X0, Y0, Z1);
                    c2 = DENS(X1, Y1, Z1) - DENS(X1, Y0, Z1);
                    c3 = DENS(X0, Y0, Z1) - c0;

                }
                else
                    if (ry >= rx && rx >= rz) {

                        c1 = DENS(X1, Y1, Z0) - DENS(X0, Y1, Z0);
                        c2 = DENS(X0, Y1, Z0) - c0;
                        c3 = DENS(X1, Y1, Z1) - DENS(X1, Y1, Z0);

                    }
                    else
                        if (ry >= rz && rz >= rx) {

                            c1 = DENS(X1, Y1, Z1) - DENS(X0, Y1, Z1);
                            c2 = DENS(X0, Y1, Z0) - c0;
                            c3 = DENS(X0, Y1, Z1) - DENS(X0, Y1, Z0);

                        }
                        else
                            if (rz >= ry && ry >= rx) {

                                c1 = DENS(X1, Y1, Z1) - DENS(X0, Y1, Z1);
                                c2 = DENS(X0, Y1, Z1) - DENS(X0, Y0, Z1);
                                c3 = DENS(X0, Y0, Z1) - c0;

                            }
                            else  {
                                c1 = c2 = c3 = 0;
                            }

       Output[OutChan] = c0 + c1 * rx + c2 * ry + c3 * rz;
       }

}

#undef DENS



#define DENS(i,j,k) (LutTable[(i)+(j)+(k)+OutChan])

static
void TetrahedralInterp16(register const cmsUInt16Number Input[],
                         register cmsUInt16Number Output[],
                         register const cmsInterpParams* p)
{
    const cmsUInt16Number* LutTable = (cmsUInt16Number*) p -> Table;
    cmsS15Fixed16Number    fx, fy, fz;
    cmsS15Fixed16Number    rx, ry, rz;
    int                    x0, y0, z0;
    cmsS15Fixed16Number    c0, c1, c2, c3, Rest;
    cmsUInt32Number        OutChan;
    cmsS15Fixed16Number    X0, X1, Y0, Y1, Z0, Z1;
    cmsUInt32Number        TotalOut = p -> nOutputs;


    fx  = _cmsToFixedDomain((int) Input[0] * p -> Domain[0]);
    fy  = _cmsToFixedDomain((int) Input[1] * p -> Domain[1]);
    fz  = _cmsToFixedDomain((int) Input[2] * p -> Domain[2]);

    x0  = FIXED_TO_INT(fx);
    y0  = FIXED_TO_INT(fy);
    z0  = FIXED_TO_INT(fz);

    rx  = FIXED_REST_TO_INT(fx);
    ry  = FIXED_REST_TO_INT(fy);
    rz  = FIXED_REST_TO_INT(fz);

    X0 = p -> opta[2] * x0;
    X1 = X0 + (Input[0] == 0xFFFFU ? 0 : p->opta[2]);

    Y0 = p -> opta[1] * y0;
    Y1 = Y0 + (Input[1] == 0xFFFFU ? 0 : p->opta[1]);

    Z0 = p -> opta[0] * z0;
    Z1 = Z0 + (Input[2] == 0xFFFFU ? 0 : p->opta[0]);

    // These are the 6 Tetrahedral
    for (OutChan=0; OutChan < TotalOut; OutChan++) {

        c0 = DENS(X0, Y0, Z0);

        if (rx >= ry && ry >= rz) {

            c1 = DENS(X1, Y0, Z0) - c0;
            c2 = DENS(X1, Y1, Z0) - DENS(X1, Y0, Z0);
            c3 = DENS(X1, Y1, Z1) - DENS(X1, Y1, Z0);

        }
        else
            if (rx >= rz && rz >= ry) {

                c1 = DENS(X1, Y0, Z0) - c0;
                c2 = DENS(X1, Y1, Z1) - DENS(X1, Y0, Z1);
                c3 = DENS(X1, Y0, Z1) - DENS(X1, Y0, Z0);

            }
            else
                if (rz >= rx && rx >= ry) {

                    c1 = DENS(X1, Y0, Z1) - DENS(X0, Y0, Z1);
                    c2 = DENS(X1, Y1, Z1) - DENS(X1, Y0, Z1);
                    c3 = DENS(X0, Y0, Z1) - c0;

                }
                else
                    if (ry >= rx && rx >= rz) {

                        c1 = DENS(X1, Y1, Z0) - DENS(X0, Y1, Z0);
                        c2 = DENS(X0, Y1, Z0) - c0;
                        c3 = DENS(X1, Y1, Z1) - DENS(X1, Y1, Z0);

                    }
                    else
                        if (ry >= rz && rz >= rx) {

                            c1 = DENS(X1, Y1, Z1) - DENS(X0, Y1, Z1);
                            c2 = DENS(X0, Y1, Z0) - c0;
                            c3 = DENS(X0, Y1, Z1) - DENS(X0, Y1, Z0);

                        }
                        else
                            if (rz >= ry && ry >= rx) {

                                c1 = DENS(X1, Y1, Z1) - DENS(X0, Y1, Z1);
                                c2 = DENS(X0, Y1, Z1) - DENS(X0, Y0, Z1);
                                c3 = DENS(X0, Y0, Z1) - c0;

                            }
                            else  {
                                c1 = c2 = c3 = 0;
                            }

                            Rest = c1 * rx + c2 * ry + c3 * rz;

                            Output[OutChan] = (cmsUInt16Number) c0 + ROUND_FIXED_TO_INT(_cmsToFixedDomain(Rest));
    }

}
#undef DENS


#define DENS(i,j,k) (LutTable[(i)+(j)+(k)+OutChan])
static
void Eval4Inputs(register const cmsUInt16Number Input[],
                     register cmsUInt16Number Output[],
                     register const cmsInterpParams* p16)
{
    const cmsUInt16Number* LutTable = (cmsUInt16Number*) p16 -> Table;
    cmsS15Fixed16Number fk;
    cmsS15Fixed16Number k0, rk;
    int K0, K1;
    cmsS15Fixed16Number    fx, fy, fz;
    cmsS15Fixed16Number    rx, ry, rz;
    int                    x0, y0, z0;
    cmsS15Fixed16Number    X0, X1, Y0, Y1, Z0, Z1;
    cmsUInt32Number i;
    cmsS15Fixed16Number    c0, c1, c2, c3, Rest;
    cmsUInt32Number        OutChan;
    cmsUInt16Number        Tmp1[MAX_STAGE_CHANNELS], Tmp2[MAX_STAGE_CHANNELS];


    fk  = _cmsToFixedDomain((int) Input[0] * p16 -> Domain[0]);
    fx  = _cmsToFixedDomain((int) Input[1] * p16 -> Domain[1]);
    fy  = _cmsToFixedDomain((int) Input[2] * p16 -> Domain[2]);
    fz  = _cmsToFixedDomain((int) Input[3] * p16 -> Domain[3]);

    k0  = FIXED_TO_INT(fk);
    x0  = FIXED_TO_INT(fx);
    y0  = FIXED_TO_INT(fy);
    z0  = FIXED_TO_INT(fz);

    rk  = FIXED_REST_TO_INT(fk);
    rx  = FIXED_REST_TO_INT(fx);
    ry  = FIXED_REST_TO_INT(fy);
    rz  = FIXED_REST_TO_INT(fz);

    K0 = p16 -> opta[3] * k0;
    K1 = K0 + (Input[0] == 0xFFFFU ? 0 : p16->opta[3]);

    X0 = p16 -> opta[2] * x0;
    X1 = X0 + (Input[1] == 0xFFFFU ? 0 : p16->opta[2]);

    Y0 = p16 -> opta[1] * y0;
    Y1 = Y0 + (Input[2] == 0xFFFFU ? 0 : p16->opta[1]);

    Z0 = p16 -> opta[0] * z0;
    Z1 = Z0 + (Input[3] == 0xFFFFU ? 0 : p16->opta[0]);

    LutTable = (cmsUInt16Number*) p16 -> Table;
    LutTable += K0;

    for (OutChan=0; OutChan < p16 -> nOutputs; OutChan++) {

        c0 = DENS(X0, Y0, Z0);

        if (rx >= ry && ry >= rz) {

            c1 = DENS(X1, Y0, Z0) - c0;
            c2 = DENS(X1, Y1, Z0) - DENS(X1, Y0, Z0);
            c3 = DENS(X1, Y1, Z1) - DENS(X1, Y1, Z0);

        }
        else
            if (rx >= rz && rz >= ry) {

                c1 = DENS(X1, Y0, Z0) - c0;
                c2 = DENS(X1, Y1, Z1) - DENS(X1, Y0, Z1);
                c3 = DENS(X1, Y0, Z1) - DENS(X1, Y0, Z0);

            }
            else
                if (rz >= rx && rx >= ry) {

                    c1 = DENS(X1, Y0, Z1) - DENS(X0, Y0, Z1);
                    c2 = DENS(X1, Y1, Z1) - DENS(X1, Y0, Z1);
                    c3 = DENS(X0, Y0, Z1) - c0;

                }
                else
                    if (ry >= rx && rx >= rz) {

                        c1 = DENS(X1, Y1, Z0) - DENS(X0, Y1, Z0);
                        c2 = DENS(X0, Y1, Z0) - c0;
                        c3 = DENS(X1, Y1, Z1) - DENS(X1, Y1, Z0);

                    }
                    else
                        if (ry >= rz && rz >= rx) {

                            c1 = DENS(X1, Y1, Z1) - DENS(X0, Y1, Z1);
                            c2 = DENS(X0, Y1, Z0) - c0;
                            c3 = DENS(X0, Y1, Z1) - DENS(X0, Y1, Z0);

                        }
                        else
                            if (rz >= ry && ry >= rx) {

                                c1 = DENS(X1, Y1, Z1) - DENS(X0, Y1, Z1);
                                c2 = DENS(X0, Y1, Z1) - DENS(X0, Y0, Z1);
                                c3 = DENS(X0, Y0, Z1) - c0;

                            }
                            else  {
                                c1 = c2 = c3 = 0;
                            }

                            Rest = c1 * rx + c2 * ry + c3 * rz;

                            Tmp1[OutChan] = (cmsUInt16Number) c0 + ROUND_FIXED_TO_INT(_cmsToFixedDomain(Rest));
    }


    LutTable = (cmsUInt16Number*) p16 -> Table;
    LutTable += K1;

    for (OutChan=0; OutChan < p16 -> nOutputs; OutChan++) {

        c0 = DENS(X0, Y0, Z0);

        if (rx >= ry && ry >= rz) {

            c1 = DENS(X1, Y0, Z0) - c0;
            c2 = DENS(X1, Y1, Z0) - DENS(X1, Y0, Z0);
            c3 = DENS(X1, Y1, Z1) - DENS(X1, Y1, Z0);

        }
        else
            if (rx >= rz && rz >= ry) {

                c1 = DENS(X1, Y0, Z0) - c0;
                c2 = DENS(X1, Y1, Z1) - DENS(X1, Y0, Z1);
                c3 = DENS(X1, Y0, Z1) - DENS(X1, Y0, Z0);

            }
            else
                if (rz >= rx && rx >= ry) {

                    c1 = DENS(X1, Y0, Z1) - DENS(X0, Y0, Z1);
                    c2 = DENS(X1, Y1, Z1) - DENS(X1, Y0, Z1);
                    c3 = DENS(X0, Y0, Z1) - c0;

                }
                else
                    if (ry >= rx && rx >= rz) {

                        c1 = DENS(X1, Y1, Z0) - DENS(X0, Y1, Z0);
                        c2 = DENS(X0, Y1, Z0) - c0;
                        c3 = DENS(X1, Y1, Z1) - DENS(X1, Y1, Z0);

                    }
                    else
                        if (ry >= rz && rz >= rx) {

                            c1 = DENS(X1, Y1, Z1) - DENS(X0, Y1, Z1);
                            c2 = DENS(X0, Y1, Z0) - c0;
                            c3 = DENS(X0, Y1, Z1) - DENS(X0, Y1, Z0);

                        }
                        else
                            if (rz >= ry && ry >= rx) {

                                c1 = DENS(X1, Y1, Z1) - DENS(X0, Y1, Z1);
                                c2 = DENS(X0, Y1, Z1) - DENS(X0, Y0, Z1);
                                c3 = DENS(X0, Y0, Z1) - c0;

                            }
                            else  {
                                c1 = c2 = c3 = 0;
                            }

                            Rest = c1 * rx + c2 * ry + c3 * rz;

                            Tmp2[OutChan] = (cmsUInt16Number) c0 + ROUND_FIXED_TO_INT(_cmsToFixedDomain(Rest));
    }



    for (i=0; i < p16 -> nOutputs; i++) {
        Output[i] = LinearInterp(rk, Tmp1[i], Tmp2[i]);
    }
}
#undef DENS


// For more that 3 inputs (i.e., CMYK)
// evaluate two 3-dimensional interpolations and then linearly interpolate between them.


static
void Eval4InputsFloat(const cmsFloat32Number Input[],
                      cmsFloat32Number Output[],
                      const cmsInterpParams* p)
{
       const cmsFloat32Number* LutTable = (cmsFloat32Number*) p -> Table;
       cmsFloat32Number rest;
       cmsFloat32Number pk;
       int k0, K0, K1;
       const cmsFloat32Number* T;
       cmsUInt32Number i;
       cmsFloat32Number Tmp1[MAX_STAGE_CHANNELS], Tmp2[MAX_STAGE_CHANNELS];
       cmsInterpParams p1;


       pk = Input[0] * p->Domain[0];
       k0 = _cmsQuickFloor(pk);
       rest = pk - (cmsFloat32Number) k0;

       K0 = p -> opta[3] * k0;
       K1 = K0 + (Input[0] >= 1.0 ? 0 : p->opta[3]);

       p1 = *p;
       memmove(&p1.Domain[0], &p ->Domain[1], 3*sizeof(cmsUInt32Number));

       T = LutTable + K0;
       p1.Table = T;

       TetrahedralInterpFloat(Input + 1,  Tmp1, &p1);

       T = LutTable + K1;
       p1.Table = T;
       TetrahedralInterpFloat(Input + 1,  Tmp2, &p1);

       for (i=0; i < p -> nOutputs; i++)
       {
              cmsFloat32Number y0 = Tmp1[i];
              cmsFloat32Number y1 = Tmp2[i];

              Output[i] = y0 + (y1 - y0) * rest;
       }
}


static
void Eval5Inputs(register const cmsUInt16Number Input[],
                 register cmsUInt16Number Output[],

                 register const cmsInterpParams* p16)
{
       const cmsUInt16Number* LutTable = (cmsUInt16Number*) p16 -> Table;
       cmsS15Fixed16Number fk;
       cmsS15Fixed16Number k0, rk;
       int K0, K1;
       const cmsUInt16Number* T;
       cmsUInt32Number i;
       cmsUInt16Number Tmp1[MAX_STAGE_CHANNELS], Tmp2[MAX_STAGE_CHANNELS];
       cmsInterpParams p1;


       fk = _cmsToFixedDomain((cmsS15Fixed16Number) Input[0] * p16 -> Domain[0]);
       k0 = FIXED_TO_INT(fk);
       rk = FIXED_REST_TO_INT(fk);

       K0 = p16 -> opta[4] * k0;
       K1 = p16 -> opta[4] * (k0 + (Input[0] != 0xFFFFU ? 1 : 0));

       p1 = *p16;
       memmove(&p1.Domain[0], &p16 ->Domain[1], 4*sizeof(cmsUInt32Number));

       T = LutTable + K0;
       p1.Table = T;

       Eval4Inputs(Input + 1, Tmp1, &p1);

       T = LutTable + K1;
       p1.Table = T;

       Eval4Inputs(Input + 1, Tmp2, &p1);

       for (i=0; i < p16 -> nOutputs; i++) {

              Output[i] = LinearInterp(rk, Tmp1[i], Tmp2[i]);
       }

}


static
void Eval5InputsFloat(const cmsFloat32Number Input[],
                      cmsFloat32Number Output[],
                      const cmsInterpParams* p)
{
       const cmsFloat32Number* LutTable = (cmsFloat32Number*) p -> Table;
       cmsFloat32Number rest;
       cmsFloat32Number pk;
       int k0, K0, K1;
       const cmsFloat32Number* T;
       cmsUInt32Number i;
       cmsFloat32Number Tmp1[MAX_STAGE_CHANNELS], Tmp2[MAX_STAGE_CHANNELS];
       cmsInterpParams p1;

       pk = Input[0] * p->Domain[0];
       k0 = _cmsQuickFloor(pk);
       rest = pk - (cmsFloat32Number) k0;

       K0 = p -> opta[4] * k0;
       K1 = K0 + (Input[0] >= 1.0 ? 0 : p->opta[4]);

       p1 = *p;
       memmove(&p1.Domain[0], &p ->Domain[1], 4*sizeof(cmsUInt32Number));

       T = LutTable + K0;
       p1.Table = T;

       Eval4InputsFloat(Input + 1,  Tmp1, &p1);

       T = LutTable + K1;
       p1.Table = T;

       Eval4InputsFloat(Input + 1,  Tmp2, &p1);

       for (i=0; i < p -> nOutputs; i++) {

              cmsFloat32Number y0 = Tmp1[i];
              cmsFloat32Number y1 = Tmp2[i];

              Output[i] = y0 + (y1 - y0) * rest;
       }
}



static
void Eval6Inputs(register const cmsUInt16Number Input[],
                 register cmsUInt16Number Output[],
                 register const cmsInterpParams* p16)
{
       const cmsUInt16Number* LutTable = (cmsUInt16Number*) p16 -> Table;
       cmsS15Fixed16Number fk;
       cmsS15Fixed16Number k0, rk;
       int K0, K1;
       const cmsUInt16Number* T;
       cmsUInt32Number i;
       cmsUInt16Number Tmp1[MAX_STAGE_CHANNELS], Tmp2[MAX_STAGE_CHANNELS];
       cmsInterpParams p1;

       fk = _cmsToFixedDomain((cmsS15Fixed16Number) Input[0] * p16 -> Domain[0]);
       k0 = FIXED_TO_INT(fk);
       rk = FIXED_REST_TO_INT(fk);

       K0 = p16 -> opta[5] * k0;
       K1 = p16 -> opta[5] * (k0 + (Input[0] != 0xFFFFU ? 1 : 0));

       p1 = *p16;
       memmove(&p1.Domain[0], &p16 ->Domain[1], 5*sizeof(cmsUInt32Number));

       T = LutTable + K0;
       p1.Table = T;

       Eval5Inputs(Input + 1, Tmp1, &p1);

       T = LutTable + K1;
       p1.Table = T;

       Eval5Inputs(Input + 1, Tmp2, &p1);

       for (i=0; i < p16 -> nOutputs; i++) {

              Output[i] = LinearInterp(rk, Tmp1[i], Tmp2[i]);
       }

}


static
void Eval6InputsFloat(const cmsFloat32Number Input[],
                      cmsFloat32Number Output[],
                      const cmsInterpParams* p)
{
       const cmsFloat32Number* LutTable = (cmsFloat32Number*) p -> Table;
       cmsFloat32Number rest;
       cmsFloat32Number pk;
       int k0, K0, K1;
       const cmsFloat32Number* T;
       cmsUInt32Number i;
       cmsFloat32Number Tmp1[MAX_STAGE_CHANNELS], Tmp2[MAX_STAGE_CHANNELS];
       cmsInterpParams p1;

       pk = Input[0] * p->Domain[0];
       k0 = _cmsQuickFloor(pk);
       rest = pk - (cmsFloat32Number) k0;

       K0 = p -> opta[5] * k0;
       K1 = K0 + (Input[0] >= 1.0 ? 0 : p->opta[5]);

       p1 = *p;
       memmove(&p1.Domain[0], &p ->Domain[1], 5*sizeof(cmsUInt32Number));

       T = LutTable + K0;
       p1.Table = T;

       Eval5InputsFloat(Input + 1,  Tmp1, &p1);

       T = LutTable + K1;
       p1.Table = T;

       Eval5InputsFloat(Input + 1,  Tmp2, &p1);

       for (i=0; i < p -> nOutputs; i++) {

              cmsFloat32Number y0 = Tmp1[i];
              cmsFloat32Number y1 = Tmp2[i];

              Output[i] = y0 + (y1 - y0) * rest;
       }
}


static
void Eval7Inputs(register const cmsUInt16Number Input[],
                 register cmsUInt16Number Output[],
                 register const cmsInterpParams* p16)
{
       const cmsUInt16Number* LutTable = (cmsUInt16Number*) p16 -> Table;
       cmsS15Fixed16Number fk;
       cmsS15Fixed16Number k0, rk;
       int K0, K1;
       const cmsUInt16Number* T;
       cmsUInt32Number i;
       cmsUInt16Number Tmp1[MAX_STAGE_CHANNELS], Tmp2[MAX_STAGE_CHANNELS];
       cmsInterpParams p1;


       fk = _cmsToFixedDomain((cmsS15Fixed16Number) Input[0] * p16 -> Domain[0]);
       k0 = FIXED_TO_INT(fk);
       rk = FIXED_REST_TO_INT(fk);

       K0 = p16 -> opta[6] * k0;
       K1 = p16 -> opta[6] * (k0 + (Input[0] != 0xFFFFU ? 1 : 0));

       p1 = *p16;
       memmove(&p1.Domain[0], &p16 ->Domain[1], 5*sizeof(cmsUInt32Number));

       T = LutTable + K0;
       p1.Table = T;

       Eval6Inputs(Input + 1, Tmp1, &p1);

       T = LutTable + K1;
       p1.Table = T;

       Eval6Inputs(Input + 1, Tmp2, &p1);

       for (i=0; i < p16 -> nOutputs; i++) {
              Output[i] = LinearInterp(rk, Tmp1[i], Tmp2[i]);
       }
}


static
void Eval7InputsFloat(const cmsFloat32Number Input[],
                      cmsFloat32Number Output[],
                      const cmsInterpParams* p)
{
       const cmsFloat32Number* LutTable = (cmsFloat32Number*) p -> Table;
       cmsFloat32Number rest;
       cmsFloat32Number pk;
       int k0, K0, K1;
       const cmsFloat32Number* T;
       cmsUInt32Number i;
       cmsFloat32Number Tmp1[MAX_STAGE_CHANNELS], Tmp2[MAX_STAGE_CHANNELS];
       cmsInterpParams p1;

       pk = Input[0] * p->Domain[0];
       k0 = _cmsQuickFloor(pk);
       rest = pk - (cmsFloat32Number) k0;

       K0 = p -> opta[6] * k0;
       K1 = K0 + (Input[0] >= 1.0 ? 0 : p->opta[6]);

       p1 = *p;
       memmove(&p1.Domain[0], &p ->Domain[1], 6*sizeof(cmsUInt32Number));

       T = LutTable + K0;
       p1.Table = T;

       Eval6InputsFloat(Input + 1,  Tmp1, &p1);

       T = LutTable + K1;
       p1.Table = T;

       Eval6InputsFloat(Input + 1,  Tmp2, &p1);


       for (i=0; i < p -> nOutputs; i++) {

              cmsFloat32Number y0 = Tmp1[i];
              cmsFloat32Number y1 = Tmp2[i];

              Output[i] = y0 + (y1 - y0) * rest;

       }
}

static
void Eval8Inputs(register const cmsUInt16Number Input[],
                 register cmsUInt16Number Output[],
                 register const cmsInterpParams* p16)
{
       const cmsUInt16Number* LutTable = (cmsUInt16Number*) p16 -> Table;
       cmsS15Fixed16Number fk;
       cmsS15Fixed16Number k0, rk;
       int K0, K1;
       const cmsUInt16Number* T;
       cmsUInt32Number i;
       cmsUInt16Number Tmp1[MAX_STAGE_CHANNELS], Tmp2[MAX_STAGE_CHANNELS];
       cmsInterpParams p1;

       fk = _cmsToFixedDomain((cmsS15Fixed16Number) Input[0] * p16 -> Domain[0]);
       k0 = FIXED_TO_INT(fk);
       rk = FIXED_REST_TO_INT(fk);

       K0 = p16 -> opta[7] * k0;
       K1 = p16 -> opta[7] * (k0 + (Input[0] != 0xFFFFU ? 1 : 0));

       p1 = *p16;
       memmove(&p1.Domain[0], &p16 ->Domain[1], 7*sizeof(cmsUInt32Number));

       T = LutTable + K0;
       p1.Table = T;

       Eval7Inputs(Input + 1, Tmp1, &p1);

       T = LutTable + K1;
       p1.Table = T;
       Eval7Inputs(Input + 1, Tmp2, &p1);

       for (i=0; i < p16 -> nOutputs; i++) {
              Output[i] = LinearInterp(rk, Tmp1[i], Tmp2[i]);
       }
}



static
void Eval8InputsFloat(const cmsFloat32Number Input[],
                      cmsFloat32Number Output[],
                      const cmsInterpParams* p)
{
       const cmsFloat32Number* LutTable = (cmsFloat32Number*) p -> Table;
       cmsFloat32Number rest;
       cmsFloat32Number pk;
       int k0, K0, K1;
       const cmsFloat32Number* T;
       cmsUInt32Number i;
       cmsFloat32Number Tmp1[MAX_STAGE_CHANNELS], Tmp2[MAX_STAGE_CHANNELS];
       cmsInterpParams p1;

       pk = Input[0] * p->Domain[0];
       k0 = _cmsQuickFloor(pk);
       rest = pk - (cmsFloat32Number) k0;

       K0 = p -> opta[7] * k0;
       K1 = K0 + (Input[0] >= 1.0 ? 0 : p->opta[7]);

       p1 = *p;
       memmove(&p1.Domain[0], &p ->Domain[1], 7*sizeof(cmsUInt32Number));

       T = LutTable + K0;
       p1.Table = T;

       Eval7InputsFloat(Input + 1,  Tmp1, &p1);

       T = LutTable + K1;
       p1.Table = T;

       Eval7InputsFloat(Input + 1,  Tmp2, &p1);


       for (i=0; i < p -> nOutputs; i++) {

              cmsFloat32Number y0 = Tmp1[i];
              cmsFloat32Number y1 = Tmp2[i];

              Output[i] = y0 + (y1 - y0) * rest;
       }
}

// The default factory
static
cmsInterpFunction DefaultInterpolatorsFactory(cmsUInt32Number nInputChannels, cmsUInt32Number nOutputChannels, cmsUInt32Number dwFlags)
{

    cmsInterpFunction Interpolation;
    cmsBool  IsFloat     = (dwFlags & CMS_LERP_FLAGS_FLOAT);
    cmsBool  IsTrilinear = (dwFlags & CMS_LERP_FLAGS_TRILINEAR);

    memset(&Interpolation, 0, sizeof(Interpolation));

    // Safety check
    if (nInputChannels >= 4 && nOutputChannels >= MAX_STAGE_CHANNELS)
        return Interpolation;

    switch (nInputChannels) {

           case 1: // Gray LUT / linear

               if (nOutputChannels == 1) {

                   if (IsFloat)
                       Interpolation.LerpFloat = LinLerp1Dfloat;
                   else
                       Interpolation.Lerp16 = LinLerp1D;

               }
               else {

                   if (IsFloat)
                       Interpolation.LerpFloat = Eval1InputFloat;
                   else
                       Interpolation.Lerp16 = Eval1Input;
               }
               break;


           case 3:  // RGB et al

               if (IsTrilinear) {

                   if (IsFloat)
                       Interpolation.LerpFloat = TrilinearInterpFloat;
                   else
                       Interpolation.Lerp16 = TrilinearInterp16;
               }
               else {

                   if (IsFloat)
                       Interpolation.LerpFloat = TetrahedralInterpFloat;
                   else {

                       Interpolation.Lerp16 = TetrahedralInterp16;
                   }
               }
               break;

           case 4:  // CMYK lut

               if (IsFloat)
                   Interpolation.LerpFloat =  Eval4InputsFloat;
               else
                   Interpolation.Lerp16    =  Eval4Inputs;
               break;

           case 5: // 5 Inks
               if (IsFloat)
                   Interpolation.LerpFloat =  Eval5InputsFloat;
               else
                   Interpolation.Lerp16    =  Eval5Inputs;
               break;

           case 6: // 6 Inks
               if (IsFloat)
                   Interpolation.LerpFloat =  Eval6InputsFloat;
               else
                   Interpolation.Lerp16    =  Eval6Inputs;
               break;

           case 7: // 7 inks
               if (IsFloat)
                   Interpolation.LerpFloat =  Eval7InputsFloat;
               else
                   Interpolation.Lerp16    =  Eval7Inputs;
               break;

           case 8: // 8 inks
               if (IsFloat)
                   Interpolation.LerpFloat =  Eval8InputsFloat;
               else
                   Interpolation.Lerp16    =  Eval8Inputs;
               break;

               break;

           default:
               Interpolation.Lerp16 = NULL;
    }

    return Interpolation;
}