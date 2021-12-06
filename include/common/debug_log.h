/*******************************************************************************
 * Copyright 2020 ModalAI Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * 4. The Software is used solely in conjunction with devices provided by
 *    ModalAI Inc.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 ******************************************************************************/

#ifndef DEBUG_LOG_H
#define DEBUG_LOG_H


// Different debug levels
enum DebugLevel
{
	ALL = 0,            ///< Log everything
	INFO,               ///< Enable info only logs
	WARNING,            ///< Enable warning only logs
	ERROR,              ///< Enable non fatal error logs
	FATAL,              ///< Enable fatal only logs
	MAX_DEBUG_LEVELS
};

void SetDebugLevel(DebugLevel level);

DebugLevel GetDebugLevel();

// Core function to print the debug log message
void DebugPrint(DebugLevel level,			///< Debug level
			    const char * format, ...);	///< Print args


#define VOXL_LOG_ALL(x...) 	    DebugPrint(DebugLevel::ALL,     x)
#define VOXL_LOG_INFO(x...)     DebugPrint(DebugLevel::INFO,    x)
#define VOXL_LOG_WARNING(x...)  DebugPrint(DebugLevel::WARNING, x)
#define VOXL_LOG_ERROR(x...)    DebugPrint(DebugLevel::ERROR,   x)
#define VOXL_LOG_FATAL(x...)    DebugPrint(DebugLevel::FATAL,   x)

#endif