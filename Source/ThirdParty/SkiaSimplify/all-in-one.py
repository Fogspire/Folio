
import sys
import os
import re
import shutil

workDir = sys.argv[1] if len(sys.argv) > 1 else '.'
withSvgParser = True

# Build dependency tree
reInclude = re.compile(r'#\s*include\s+[<"]([^>"]+)[>"]')
allFiles = [ ]
fileDeps = { }
fileBlacklist = set()

def parseIncludes(sourceCode):
    includes = [ ]
    matches = reInclude.findall(sourceCode)
    for match in matches:
        includes.append(match)
    return includes

def mapSourceDirectory(dirName):
    dirPath = os.path.join(workDir, dirName)
    for root, dirs, files in os.walk(dirPath):
        for fileName in files:
            if fileName.lower().endswith(('.h', '.hpp', '.c', '.cpp')):
                relPath = os.path.join(os.path.relpath(root, workDir), fileName).replace('\\', '/')
                if relPath in fileBlacklist:
                    continue
                filePath = os.path.join(root, fileName)
                allFiles.append(relPath)
                with open(filePath, 'r', encoding='utf-8') as f:
                    fileDeps[relPath] = parseIncludes(f.read())

fileBlacklist.add('src/core/SkOSFile.h')

mapSourceDirectory('include')
mapSourceDirectory('src')

# Transitive dependencies
def listTransitiveDependencies(filePath, listedDeps, notFoundDeps):
    deps = [ ]
    if filePath not in listedDeps:
        if filePath in fileDeps:
            for dep in fileDeps[filePath]:
                deps.extend(listTransitiveDependencies(dep, listedDeps, notFoundDeps))
            if filePath not in listedDeps:
                deps.append(filePath)
                listedDeps.add(filePath)
        else:
            notFoundDeps.add(filePath)
    return deps

def listDependencies(paths):
    listedDeps = set()
    notFoundDeps = set()
    deps = [ ]
    for path in paths:
        deps.extend(listTransitiveDependencies(path, listedDeps, notFoundDeps))
    print('Not found:', notFoundDeps)
    return deps

# Organize files
mainHeaders = [
    'include/core/SkPath.h',
    'include/pathops/SkPathOps.h'
]
if withSvgParser:
    mainHeaders.append('include/utils/SkParsePath.h')

publicHeaders = listDependencies(mainHeaders)
publicHeaderSet = set(publicHeaders)
orderedFiles = listDependencies(allFiles)

privateHeaders = [ ]
privateSources = [ ]

for file in orderedFiles:
    if file.lower().endswith('.cpp'):
        privateSources.append(file)
    elif file not in publicHeaderSet:
        privateHeaders.append(file)

# Generate all-in-one
allInOneDir = os.path.join(workDir, 'all-in-one')
os.makedirs(allInOneDir, exist_ok=True)

header = """
/*
 * Copyright 2012 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* THIS IS A SUBSET OF THE SKIA LIBRARY'S PATHOPS MODULE WITH ONLY THE FOLLOWING FUNCTIONALITY:
 *   - Path simplification
 *   - Path boolean operations
 *   - Path parser from SVG representation
 */

#pragma once

#include <algorithm>
#include <atomic>
#include <climits>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <memory>
#include <new>
#include <tuple>
#include <type_traits>
#include <utility>

#if defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>
#endif

#define TArray SKIA_TArray

#define SKNX_NO_SIMD

#ifdef SKIA_SIMPLIFY_NAMESPACE
namespace SKIA_SIMPLIFY_NAMESPACE {
#endif
"""
configEnd = """
#if !defined(SkDebugf) && !defined(SKDEBUGF_IMPLEMENTED)
#define SkDebugf(...) ((void) 0)
#endif
"""
headerEnd = """
#ifdef SKIA_SIMPLIFY_NAMESPACE
} // namespace SKIA_SIMPLIFY_NAMESPACE
#endif

#undef TArray
"""

source = """
/*
 * Copyright 2012 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <array>
#include <cassert>
#include <cfloat>
#include <cstdarg>
#include <cstdlib>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wundef"
#pragma GCC diagnostic ignored "-Wunused"
#pragma GCC diagnostic ignored "-Wundefined-internal"
#elif defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4100 4127 4189 4267 4389 4456 4457 4458 4459 4668 4701 4706 4800 4996 5046 6001 6011 6246 6282 6297 6323 6385 6386 28182)
#endif

#include "skia-simplify.h"

#if defined(_WIN32) || defined(__SYMBIAN32__)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#undef near
#undef far
#else
#include <pthread.h>
#ifdef __APPLE__
#include <malloc/malloc.h>
#include <dispatch/dispatch.h>
#else
#include <malloc.h>
#include <semaphore.h>
#endif
#endif

#undef PI
#define TArray SKIA_TArray
#define Top SKIA_Top

#ifdef SKIA_SIMPLIFY_NAMESPACE
namespace SKIA_SIMPLIFY_NAMESPACE {
#endif
"""
sourceEnd = """
#ifdef SKIA_SIMPLIFY_NAMESPACE
} // namespace SKIA_SIMPLIFY_NAMESPACE
#endif

#undef TArray
#undef Top
#undef ERROR

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif
"""

reInitialComment = re.compile(r'^\s*/\*(.*?)\*/', re.DOTALL)
reInitialGuard = re.compile(r'^\s*#ifndef\s+([A-Za-z0-9_]+)\s*\n\s*#define\s+([A-Za-z0-9_]+)\s')
reEndGuard = re.compile(r'\n\s*#endif(\s+//.*)?\s*$')
def sanitizeSource(fileName, src):
    # Name collisions
    if fileName.endswith('/SkCubics.cpp'):
        src = src.replace('approximately_zero', 'skCubics_approximately_zero')
    if fileName.endswith('/SkPath.cpp'):
        src = src.replace('between', 'skPath_between')
    if fileName.endswith('/SkGeometry.cpp'):
        src = src.replace('between', 'skGeometry_between')
    if fileName.endswith('/SkParsePath.cpp'):
        src = src.replace('is_between', 'skParsePath_is_between')
        src = src.replace('is_digit', 'skParsePath_is_digit')
        src = src.replace('is_sep', 'skParsePath_is_sep')
        src = src.replace('is_ws', 'skParsePath_is_ws')
        src = src.replace('skip_sep', 'skParsePath_skip_sep')
        src = src.replace('skip_ws', 'skParsePath_skip_ws')
    if fileName.endswith('/SkPathBuilder.cpp'):
        src = src.replace('arc_is_lone_point', 'skPathBuilder_arc_is_lone_point')
        src = src.replace('angles_to_unit_vectors', 'skPathBuilder_angles_to_unit_vectors')
        src = src.replace('build_arc_conics', 'skPathBuilder_build_arc_conics')
    if fileName.endswith('/SkPathOpsCubic.cpp'):
        src = src.replace('PI', 'skPathOpsCubic_PI')
    match = reInitialComment.match(src)
    if match:
        src = src[match.end():].lstrip()
    src = '\n'.join([line for line in src.split('\n') if not re.match(r'^\s*#\s*(include\s.*|pragma\s+once)\s*$', line)])
    initialMatch = reInitialGuard.match(src)
    endMatch = reEndGuard.search(src)
    if initialMatch and endMatch and initialMatch.group(1) == initialMatch.group(2):
        src = src[:endMatch.start()][initialMatch.end():]
    if fileName.endswith('/SkUserConfig.h'):
        src += configEnd
    return src.strip()+'\n'

def processFiles(fileList):
    src = ''
    for fileName in fileList:
        filePath = os.path.join(workDir, fileName)
        with open(filePath, 'r', encoding='utf-8') as f:
            src += '\n'+sanitizeSource(fileName, f.read())
    return src

header += processFiles(publicHeaders)
source += processFiles(privateHeaders)
source += processFiles(privateSources)
header += headerEnd
source += sourceEnd

source = source.replace('auto setComputedConvexity = [=](SkPathConvexity convexity)', 'auto setComputedConvexity = [=, this](SkPathConvexity convexity)')

with open(os.path.join(allInOneDir, 'skia-simplify.h'), 'w', encoding='utf-8') as f:
    f.write(header)
with open(os.path.join(allInOneDir, 'skia-simplify.cpp'), 'w', encoding='utf-8') as f:
    f.write(source)
