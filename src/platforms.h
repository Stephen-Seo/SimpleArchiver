/*
 * Copyright 2024 Stephen Seo
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * `platforms.h` is the header that determines what platform this program is
 * compiled for.
 */

// Determine platform macros
#define SIMPLE_ARCHIVER_PLATFORM_WINDOWS 1
#define SIMPLE_ARCHIVER_PLATFORM_MAC 2
#define SIMPLE_ARCHIVER_PLATFORM_LINUX 3
#define SIMPLE_ARCHIVER_PLATFORM_COSMOPOLITAN 4
#define SIMPLE_ARCHIVER_PLATFORM_UNKNOWN 0

#if defined __COSMOPOLITAN__
# define SIMPLE_ARCHIVER_PLATFORM SIMPLE_ARCHIVER_PLATFORM_COSMOPOLITAN
#elif defined _WIN32
# define SIMPLE_ARCHIVER_PLATFORM SIMPLE_ARCHIVER_PLATFORM_WINDOWS
#elif defined __APPLE__
# define SIMPLE_ARCHIVER_PLATFORM SIMPLE_ARCHIVER_PLATFORM_MAC
#elif defined __linux__
# define SIMPLE_ARCHIVER_PLATFORM SIMPLE_ARCHIVER_PLATFORM_LINUX
#else
# define SIMPLE_ARCHIVER_PLATFORM SIMPLE_ARCHIVER_PLATFORM_UNKNOWN
#endif
