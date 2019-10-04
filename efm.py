#!/usr/bin/python3
#
# efm.py - LDS sample to EFM data processing
# Copyright (C) 2019 Simon Inns
#
# This file is part of ld-decode.
#
# efm.py is free software: you can redistribute it and/or
# modify it under the terms of the GNU General Public License as
# published by the Free Software Foundation, either version 3 of the
# License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

# Note: The PLL implementation is based on original code provided to
# the ld-decode project by Olivier "Sarayan" Galibert.  Many thanks
# for the assistance!

import numba
import numpy as np
import sys

# Attribute types of EFM_PLL for numba.
EFM_PLL_spec = [
    ("zcPreviousInput", numba.int16),
    ("prevDirection", numba.boolean),
    ("delta", numba.float64),

    ("pllResult", numba.int8[:]),
    ("pllResultCount", numba.uintp),

    ("basePeriod", numba.float64),

    ("minimumPeriod", numba.float64),
    ("maximumPeriod", numba.float64),
    ("periodAdjustBase", numba.float64),

    ("currentPeriod", numba.float64),
    ("phaseAdjust", numba.float64),
    ("refClockTime", numba.float64),
    ("frequencyHysteresis", numba.int32),
    ("tCounter", numba.int8),
]

@numba.jitclass(EFM_PLL_spec)
class EFM_PLL:
    def __init__(self):
        # ZC detector state
        self.zcPreviousInput = 0
        self.prevDirection = False # Down
        self.delta = 0.0

        # PLL output buffer
        self.pllResult = np.empty(1 << 10, np.int8) # XXX how big?
        self.pllResultCount = 0

        # PLL state
        self.basePeriod = 40000000.0 / 4321800.0 # T1 clock period 40MSPS / bit-rate

        self.minimumPeriod = self.basePeriod * 0.90 # -10% minimum
        self.maximumPeriod = self.basePeriod * 1.10 # +10% maximum
        self.periodAdjustBase = self.basePeriod * 0.0001 # Clock adjustment step

        # PLL Working parameters
        self.currentPeriod = self.basePeriod
        self.phaseAdjust = 0.0
        self.refClockTime = 0.0
        self.frequencyHysteresis = 0
        self.tCounter = 1

    def process(self, inputBuffer):
        """This method performs interpolated zero-crossing detection and stores
        the result as sample deltas (the number of samples between each
        zero-crossing).  Interpolation of the zero-crossing point provides a
        result with sub-sample resolution.

        Since the EFM data is NRZ-I (non-return to zero inverted) the polarity
        of the input signal is not important (only the frequency); therefore we
        can simply store the delta information.  The resulting delta
        information is fed to the phase-locked loop which is responsible for
        correcting jitter errors from the ZC detection process.

        inputBuffer is a numpy.ndarray of np.int16 samples.
        Returns a view into a numpy.ndarray of np.int8 times."""

        # Clear the PLL result buffer
        self.pllResultCount = 0

        for vCurr in inputBuffer:
            vPrev = self.zcPreviousInput

            xup = False
            xdn = False

            # Possing zero-cross up or down?
            if vPrev < 0 and vCurr >= 0:
                xup = True
            if vPrev > 0 and vCurr <= 0:
                xdn = True

            # Check ZC direction against previous
            if self.prevDirection and xup:
                xup = False
            if (not self.prevDirection) and xdn:
                xdn = False

            # Store the current direction as the previous
            if xup:
                self.prevDirection = True
            if xdn:
                self.prevDirection = False

            if xup or xdn:
                # Interpolate to get the ZC sub-sample position fraction
                prev = float(vPrev)
                curr = float(vCurr)
                fraction = (-prev) / (curr - prev)

                # Feed the sub-sample accurate result to the PLL
                self.pushEdge(self.delta + fraction)

                # Offset the next delta by the fractional part of the result
                # in order to maintain accuracy
                self.delta = 1.0 - fraction
            else:
                # No ZC, increase delta by 1 sample
                self.delta += 1.0

            # Keep the previous input (so we can work across buffer boundaries)
            self.zcPreviousInput = vCurr

        return self.pllResult[:self.pllResultCount]

    def pushTValue(self, bit):
        # If this is a 1, push the T delta
        if bit:
            self.pllResult[self.pllResultCount] = self.tCounter
            self.pllResultCount += 1

            self.tCounter = 1
        else:
            self.tCounter += 1

    def pushEdge(self, sampleDelta):
        """Called when a ZC happens on a sample number."""

        while sampleDelta >= self.refClockTime:
            nextTime = self.refClockTime + self.currentPeriod + self.phaseAdjust
            self.refClockTime = nextTime

            # Note: the tCounter < 3 check causes an 'edge push' if T is 1 or 2 (which
            # are invalid timing lengths for the NRZI data).  We also 'edge pull' values
            # greater than T11
            if (sampleDelta > nextTime or self.tCounter < 3) and not self.tCounter > 10:
                self.phaseAdjust = 0.0
                self.pushTValue(0)
            else:
                edgeDelta = sampleDelta - (nextTime - self.currentPeriod / 2.0)
                self.phaseAdjust = edgeDelta * 0.005

                # Adjust frequency based on error
                if edgeDelta < 0:
                    if self.frequencyHysteresis < 0:
                        self.frequencyHysteresis -= 1
                    else:
                        self.frequencyHysteresis = -1
                elif edgeDelta > 0:
                    if self.frequencyHysteresis > 0:
                        self.frequencyHysteresis += 1
                    else:
                        self.frequencyHysteresis = 1
                else:
                    self.frequencyHysteresis = 0

                # Update the reference clock?
                if self.frequencyHysteresis:
                    if self.frequencyHysteresis < -1.0 or self.frequencyHysteresis > 1.0:
                        aper = self.periodAdjustBase * edgeDelta / self.currentPeriod
                        self.currentPeriod += aper

                        if self.currentPeriod < self.minimumPeriod:
                            self.currentPeriod = self.minimumPeriod
                        elif self.currentPeriod > self.maximumPeriod:
                            self.currentPeriod = self.maximumPeriod

                self.pushTValue(1)

        # Reset refClockTime ready for the next delta but
        # keep any error to maintain accuracy
        self.refClockTime -= sampleDelta

        # Use this debug if you want to monitor the PLL output frequency
        #print("Base =", self.basePeriod, "current = ", self.currentPeriod, file=sys.stderr)

if __name__ == "__main__":
    # If invoked as a script, test the PLL by reading from stdin and writing to stdout.

    pll = EFM_PLL()

    while True:
        inputBuffer = sys.stdin.buffer.read(1 << 10)
        if len(inputBuffer) == 0:
            break

        inputData = np.frombuffer(inputBuffer, np.int16)
        outputData = pll.process(inputData)
        outputData.tofile(sys.stdout)
