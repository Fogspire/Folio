
import sys
import os
import re
import shutil

if len(sys.argv) < 2:
    print('Usage: python get-source.py <skia-directory> <target-directory>')
    sys.exit()

srcDir = sys.argv[1]
dstDir = sys.argv[2] if len(sys.argv) > 2 else '.'
withSvgParser = True

# Build dependency tree
reInclude = re.compile(r'#\s*include\s+[<"]([^>"]+)[>"]')
fileDeps = { }

def parseIncludes(sourceCode):
    includes = [ ]
    matches = reInclude.findall(sourceCode)
    for match in matches:
        includes.append(match)
    return includes

def mapSourceDirectory(dirName):
    dirPath = os.path.join(srcDir, dirName)
    for root, dirs, files in os.walk(dirPath):
        for fileName in files:
            if fileName.lower().endswith(('.h', '.hpp', '.c', '.cpp')):
                filePath = os.path.join(root, fileName)
                relPath = os.path.join(os.path.relpath(root, srcDir), fileName).replace('\\', '/')
                with open(filePath, 'r', encoding='utf-8') as f:
                    fileDeps[relPath] = parseIncludes(f.read())

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

# Cherry-pick files
mainDependencies = [
    # Required functionality
    'src/core/SkPath.cpp',
    'src/pathops/SkPathOpsSimplify.cpp',

    # Internal dependencies of the above
    'src/base/SkArenaAlloc.cpp',
    'src/base/SkBezierCurves.cpp',
    'src/base/SkBuffer.cpp',
    'src/base/SkCubics.cpp',
    'src/base/SkContainers.cpp',
    'src/base/SkFloatingPoint.cpp',
    'src/base/SkMalloc.cpp',
    'src/base/SkQuads.cpp',
    'src/base/SkSafeMath.cpp',
    'src/base/SkSemaphore.cpp',
    'src/base/SkTDArray.cpp',
    'src/base/SkThreadID.cpp',
    'src/base/SkUtils.cpp',
    'src/base/SkUTF.cpp',

    'src/core/SkCubicClipper.cpp',
    'src/core/SkEdgeClipper.cpp',
    'src/core/SkGeometry.cpp',
    'src/core/SkIDChangeListener.cpp',
    'src/core/SkLineClipper.cpp',
    'src/core/SkM44.cpp',
    'src/core/SkMatrix.cpp',
    'src/core/SkMatrixInvert.cpp',
    'src/core/SkPathRef.cpp',
    'src/core/SkPathBuilder.cpp',
    'src/core/SkPoint.cpp',
    'src/core/SkRect.cpp',
    'src/core/SkRRect.cpp',
    'src/core/SkString.cpp',
    'src/core/SkStringUtils.cpp',

    'src/pathops/SkAddIntersections.cpp',
    'src/pathops/SkDConicLineIntersection.cpp',
    'src/pathops/SkDCubicLineIntersection.cpp',
    'src/pathops/SkDLineIntersection.cpp',
    'src/pathops/SkDQuadLineIntersection.cpp',
    'src/pathops/SkIntersections.cpp',
    'src/pathops/SkOpAngle.cpp',
    'src/pathops/SkOpCoincidence.cpp',
    'src/pathops/SkOpContour.cpp',
    'src/pathops/SkOpCubicHull.cpp',
    'src/pathops/SkOpEdgeBuilder.cpp',
    'src/pathops/SkOpSegment.cpp',
    'src/pathops/SkOpSpan.cpp',
    'src/pathops/SkPathOpsCommon.cpp',
    'src/pathops/SkPathOpsConic.cpp',
    'src/pathops/SkPathOpsCubic.cpp',
    'src/pathops/SkPathOpsCurve.cpp',
    'src/pathops/SkPathOpsDebug.cpp',
    'src/pathops/SkPathOpsLine.cpp',
    'src/pathops/SkPathOpsQuad.cpp',
    'src/pathops/SkPathOpsRect.cpp',
    'src/pathops/SkPathOpsTSect.cpp',
    'src/pathops/SkPathOpsTypes.cpp',
    'src/pathops/SkPathOpsWinding.cpp',
    'src/pathops/SkPathWriter.cpp',
    'src/pathops/SkReduceOrder.cpp',

    'src/ports/SkMemory_malloc.cpp'
]

svgParserDependencies = [
    'src/utils/SkParsePath.cpp',
    'src/core/SkData.cpp',
    'src/core/SkStream.cpp',
    'src/pathops/SkPathOpsOp.cpp',
    'src/utils/SkParse.cpp'
]

if withSvgParser:
    mainDependencies.extend(svgParserDependencies)

allDependencies = listDependencies(mainDependencies)

# Copy files and their dependencies
filesCopied = 0
filesSkipped = 0
for dep in allDependencies:
    srcPath = os.path.join(srcDir, dep)
    dstPath = os.path.join(dstDir, dep)
    dstPathDir = os.path.dirname(dstPath)
    os.makedirs(dstPathDir, exist_ok=True)
    if not os.path.exists(dstPath):
        shutil.copy2(srcPath, dstPath)
        filesCopied += 1
    else:
        filesSkipped += 1
print('Copied '+str(filesCopied)+' files ('+str(filesSkipped)+' skipped)')
