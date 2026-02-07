"""Fix RenderBlocks.java by stubbing out methods that reference deleted block classes."""
import re

filepath = "mc-src/net/minecraft/client/renderer/RenderBlocks.java"
with open(filepath, 'r') as f:
    content = f.read()

# Method signatures to stub (replace body with return false/return)
stub_methods = {
    # boolean methods -> return false
    '_stripped_renderPistonBase': 'return false;',
    '_dead_piston_code_removed': 'return false;',
    'renderBlockHopper': 'return false;',
    'renderBlockHopperMetadata': 'return false;',
}

lines = content.split('\n')
result = []
i = 0
skip_depth = 0
skipping = False
current_stub = None

while i < len(lines):
    line = lines[i]
    stripped = line.strip()

    # Check if this line starts a method we want to stub
    should_stub = False
    for method_name, stub_return in stub_methods.items():
        if method_name + '(' in stripped and (stripped.startswith('public ') or stripped.startswith('private ') or stripped.startswith('protected ')):
            should_stub = True
            current_stub = stub_return
            # Check if it's already a one-liner stub
            if '{' in stripped and '}' in stripped:
                result.append(line)
                should_stub = False
                break
            break

    if should_stub:
        # Find the opening brace
        if '{' in stripped:
            # Method signature and opening brace on same line
            indent = line[:len(line) - len(line.lstrip())]
            # Change signature types if needed
            sig_line = line.replace('BlockHopper ', 'Block ').replace('(BlockHopper)', '(Block)')
            result.append(sig_line.rstrip().rstrip('{').rstrip() + ' { ' + current_stub + ' } // stripped')
            skip_depth = 1
            skipping = True
        else:
            # Signature without brace, next line should have it
            sig_line = line.replace('BlockHopper ', 'Block ').replace('(BlockHopper)', '(Block)')
            result.append(sig_line.rstrip() + ' { ' + current_stub + ' } // stripped')
            # Skip until we find the opening brace, then skip the whole body
            i += 1
            while i < len(lines) and '{' not in lines[i]:
                i += 1
            if i < len(lines):
                skip_depth = 1
                skipping = True
    elif skipping:
        # Count braces to find end of method
        for ch in stripped:
            if ch == '{':
                skip_depth += 1
            elif ch == '}':
                skip_depth -= 1
        if skip_depth <= 0:
            skipping = False
            # Don't add the closing brace (it's part of the stub)
    else:
        # Fix remaining references inline
        modified = line
        # Fix hopper references in renderBlockByRenderType (line ~8041)
        if 'renderBlockHopperMetadata((BlockHopper)' in modified:
            modified = modified.replace('renderBlockHopperMetadata((BlockHopper)', 'renderBlockHopperMetadata((Block)')
        result.append(modified)

    i += 1

with open(filepath, 'w') as f:
    f.write('\n'.join(result))

print("Done. Fixed RenderBlocks.java")
