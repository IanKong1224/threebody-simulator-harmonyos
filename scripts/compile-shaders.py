#!/usr/bin/env python3
"""Rebuild checked-in shader headers; requires official glslangValidator on PATH or GLSLANG."""
import os, pathlib, subprocess, struct, tempfile
root=pathlib.Path(__file__).resolve().parents[1]
compiler=os.environ.get('GLSLANG','glslangValidator')
# Backend wrappers only define GLSL interfaces; material/post algorithms have one source.
for api in ('vulkan', 'gles'):
 for name, body in (('scene', 'star'), ('post', 'post')):
  path = root / f'renderer/{api}/shaders/{name}.frag'
  header = path.read_text().split('// Shared', 1)[0]
  path.write_text(header + (root / f'renderer/common/shaders/{body}.frag.glsl').read_text())
parts=['#pragma once\n#include <cstdint>\nnamespace threebody {\n']
with tempfile.TemporaryDirectory() as tmp:
 for name,stage in ((n,s) for n in ('scene','post','basic','particles') for s in ('vert','frag')):
  source=root/f'renderer/vulkan/shaders/{name}.{stage}'
  binary=pathlib.Path(tmp)/f'{name}.{stage}.spv'
  subprocess.run([compiler,'-V','--target-env','vulkan1.0','-o',str(binary),str(source)],check=True)
  raw=binary.read_bytes();words=struct.unpack('<'+'I'*(len(raw)//4),raw)
  parts.append(f'constexpr uint32_t {name}_{stage}[] = {{\n')
  for i in range(0,len(words),8):parts.append(','.join(hex(w) for w in words[i:i+8])+',\n')
  parts.append('};\n')
parts.append('}\n');(root/'renderer/vulkan/SceneShaders.h').write_text(''.join(parts))
parts=['#pragma once\nnamespace threebody {\n']
for name,stage in ((n,s) for n in ('scene','post','basic','particles') for s in ('vert','frag')):
 source=(root/f'renderer/gles/shaders/{name}.{stage}').read_text()
 subprocess.run([compiler,'-S',stage,str(root/f'renderer/gles/shaders/{name}.{stage}')],check=True)
 parts.append(f'constexpr const char* {name}_{stage} = R"GLSL({source})GLSL";\n')
parts.append('}\n');(root/'renderer/gles/SceneShaders.h').write_text(''.join(parts))
