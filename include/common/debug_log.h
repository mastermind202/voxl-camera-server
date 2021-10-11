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

#include <stdio.h>

#ifdef DEBUG
///<@todo add debug assert
#define DEBUG_ASSERT(x, y)
#else
#define DEBUG_ASSERT(x)
#endif // #ifndef DEBUG

#define DISABLE_WRAP		"\033[?7l"	// disables line wrap, be sure to enable before exiting
#define ENABLE_WRAP			"\033[?7h"	// default terminal behavior

// Different debug levels
enum class DebugLevel
{
	ALL = 0,            ///< Log everything
	INFO,               ///< Enable info only logs
	WARNING,            ///< Enable warning only logs
	ERROR,              ///< Enable non fatal error logs
	FATAL,              ///< Enable fatal only logs
	MAX_DEBUG_LEVELS
};

// -----------------------------------------------------------------------------------------------------------------------------
// Static singleton class to enable debug logs
// -----------------------------------------------------------------------------------------------------------------------------
class Debug
{
public:
	// Set debug level
	///<@todo This will affect all threads if any one thread changes the debug level at runtime
	static void SetDebugLevel(DebugLevel level)
	{
		m_currentDebugLevel = level;
	}

	static DebugLevel GetDebugLevel(){
		return m_currentDebugLevel;
	}

	// Core function to print the debug log message
	static void DebugPrint(DebugLevel level,			///< Debug level
				           const char location[],		///< Print location (file, line number etc)
				           const char * format, ...);	///< Print args

private:
	// Disable instantiation
	Debug();
	// Disable destruction
	~Debug();

	static DebugLevel m_currentDebugLevel;	///< Current debug level that can be changed with SetDebugLevel()
};

// Convert to string
#define TOSTRING(x) #x

// Add file line number to the message
#define VOXL_LOG_LOCATION "(" __FILE__ ":" TOSTRING(__LINE__) "): "

#define VOXL_LOG_ALL(x...) 	    Debug::DebugPrint(DebugLevel::ALL,     VOXL_LOG_LOCATION, x)
#define VOXL_LOG_INFO(x...)     Debug::DebugPrint(DebugLevel::INFO,    VOXL_LOG_LOCATION, x)
#define VOXL_LOG_WARNING(x...)  Debug::DebugPrint(DebugLevel::WARNING, VOXL_LOG_LOCATION, x)
#define VOXL_LOG_ERROR(x...)    Debug::DebugPrint(DebugLevel::ERROR,   VOXL_LOG_LOCATION, x)
#define VOXL_LOG_FATAL(x...)    Debug::DebugPrint(DebugLevel::FATAL,   VOXL_LOG_LOCATION, x)

#endif