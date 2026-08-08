// --------------------------------------------------------------------------------------------------
//  Copyright (c) 2016 Microsoft Corporation
//  
//  Permission is hereby granted, free of charge, to any person obtaining a copy of this software and
//  associated documentation files (the "Software"), to deal in the Software without restriction,
//  including without limitation the rights to use, copy, modify, merge, publish, distribute,
//  sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
//  furnished to do so, subject to the following conditions:
//  
//  The above copyright notice and this permission notice shall be included in all copies or
//  substantial portions of the Software.
//  
//  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT
//  NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
//  NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
//  DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
// --------------------------------------------------------------------------------------------------

package com.microsoft.Malmo;

import net.minecraft.launchwrapper.IClassTransformer;

import org.objectweb.asm.ClassReader;
import org.objectweb.asm.ClassWriter;
import org.objectweb.asm.Opcodes;
import org.objectweb.asm.tree.AbstractInsnNode;
import org.objectweb.asm.tree.ClassNode;
import org.objectweb.asm.tree.FieldInsnNode;
import org.objectweb.asm.tree.InsnList;
import org.objectweb.asm.tree.InsnNode;
import org.objectweb.asm.tree.JumpInsnNode;
import org.objectweb.asm.tree.LabelNode;
import org.objectweb.asm.tree.LdcInsnNode;
import org.objectweb.asm.tree.MethodInsnNode;
import org.objectweb.asm.tree.MethodNode;
import org.objectweb.asm.tree.VarInsnNode;

public class OverclockingClassTransformer implements IClassTransformer
{
    enum transformType {  OTHERPLAYER, TEXTURES }
    
    @Override
    public byte[] transform(String name, String transformedName, byte[] basicClass)
    {
        if (transformedName.startsWith("net.minecraft.client.entity"))
            System.out.println("Transformed Name: " + transformedName);
        boolean isObfuscated = !name.equals(transformedName);
        if (transformedName.equals("net.minecraft.client.entity.EntityOtherPlayerMP"))
            return transform(basicClass, isObfuscated, transformType.OTHERPLAYER);
        else if (transformedName.equals("net.minecraft.client.renderer.GlStateManager"))
            return transform(basicClass, isObfuscated, transformType.TEXTURES);
        else if (transformedName.equals("net.minecraft.client.renderer.BlockModelRenderer$AmbientOcclusionFace"))
            return transformAo(basicClass, isObfuscated);
        else
            return basicClass;
    }

    private static byte[] transform(byte[] serverClass, boolean isObfuscated, transformType type)
    {
        try
        {
            ClassNode cnode = new ClassNode();
            ClassReader creader = new ClassReader(serverClass);
            creader.accept(cnode, 0);
            
            switch (type)
            {
            case OTHERPLAYER:
                removeInterpolation(cnode, isObfuscated);
                break;
            case TEXTURES:
                insertTextureHandler(cnode, isObfuscated);
            }
            
            ClassWriter cwriter = new ClassWriter(ClassWriter.COMPUTE_MAXS | ClassWriter.COMPUTE_FRAMES);
            cnode.accept(cwriter);
            return cwriter.toByteArray();
        }
        catch (Exception e)
        {
        }
        return serverClass;
    }

    // AO drop-in (render-opt kernel 13): rewrite BlockModelRenderer$AmbientOcclusionFace
    // .getAoBrightness so that, when netheritemod.QAOHook.MODE != 0, it returns
    // netheritemod.QAOHook.aoBrightness(br1,br2,br3,br4) (native C kernel, or 0 for sabotage) instead of
    // the vanilla body. MODE == 0 falls through to the untouched original (a true vanilla
    // baseline). Done with a raw IClassTransformer because Mixin cannot attach to this
    // package-private inner class in this dev setup (FML remapper unmaps the inner target to
    // notch). getAoBrightness is private/instance: this=slot0, br1..br4 = slots 1..4.
    private static byte[] transformAo(byte[] basicClass, boolean isObfuscated)
    {
        try
        {
            ClassNode cnode = new ClassNode();
            new ClassReader(basicClass).accept(cnode, 0);
            final String mcp = "getAoBrightness";
            final String srg = "func_147778_a";
            final String desc = "(IIII)I";
            boolean done = false;
            for (MethodNode method : cnode.methods)
            {
                if (method.desc.equals(desc) && (method.name.equals(mcp) || method.name.equals(srg)))
                {
                    InsnList pre = new InsnList();
                    LabelNode orig = new LabelNode();
                    pre.add(new FieldInsnNode(Opcodes.GETSTATIC, "netheritemod/QAOHook", "MODE", "I"));
                    pre.add(new JumpInsnNode(Opcodes.IFEQ, orig));   // MODE == 0 -> vanilla body
                    pre.add(new VarInsnNode(Opcodes.ILOAD, 1));
                    pre.add(new VarInsnNode(Opcodes.ILOAD, 2));
                    pre.add(new VarInsnNode(Opcodes.ILOAD, 3));
                    pre.add(new VarInsnNode(Opcodes.ILOAD, 4));
                    pre.add(new MethodInsnNode(Opcodes.INVOKESTATIC, "netheritemod/QAOHook", "aoBrightness", "(IIII)I", false));
                    pre.add(new InsnNode(Opcodes.IRETURN));
                    pre.add(orig);
                    method.instructions.insert(pre);
                    done = true;
                    System.out.println("MALMO/qao: rewrote getAoBrightness (" + method.name + ") with native AO hook");
                    break;
                }
            }
            if (!done)
                System.out.println("MALMO/qao: WARNING getAoBrightness " + desc + " not found in AmbientOcclusionFace");
            ClassWriter cwriter = new ClassWriter(ClassWriter.COMPUTE_MAXS | ClassWriter.COMPUTE_FRAMES);
            cnode.accept(cwriter);
            return cwriter.toByteArray();
        }
        catch (Exception e)
        {
            System.out.println("MALMO/qao: transformAo failed: " + e);
            e.printStackTrace();
        }
        return basicClass;
    }

    private static void removeInterpolation(ClassNode node, boolean isObfuscated)
    {
        // We're attempting to turn this line from EntityOtherPlayerMP.func_180426_a:
        //              this.otherPlayerMPPosRotationIncrements = p_180426_9_;
        // into this:
        //              this.otherPlayerMPPosRotationIncrements = 1;
        final String methodName = "func_180426_a";
        final String methodDescriptor = "(DDDFFIZ)V"; // double x/y/z, float yaw/pitch, int increments, bool (unused), returns void.

        System.out.println("MALMO: Found EntityOtherPlayerMP, attempting to transform it");
        for (MethodNode method : node.methods)
        {
            if (method.name.equals(methodName) && method.desc.equals(methodDescriptor))
            {
                System.out.println("MALMO: Found EntityOtherPlayerMP.func_180426_a() method, attempting to transform it");
                for (AbstractInsnNode instruction : method.instructions.toArray())
                {
                    if (instruction.getOpcode() == Opcodes.ILOAD)
                    {
                        LdcInsnNode newNode = new LdcInsnNode(new Integer(1));
                        method.instructions.insert(instruction, newNode);
                        method.instructions.remove(instruction);
                        return;
                    }
                }
            }
        }
    }

    private static void insertTextureHandler(ClassNode node, boolean isObfuscated)
    {
        // We're attempting to turn this line from GlStateManager.bindTexture:
        //      GL11.glBindTexture(GL11.GL_TEXTURE_2D, texture);
        // into this:
        //      TextureHelper.glBindTexture(GL11.GL_TEXTURE_2D, texture);
        // TextureHelpers's method then decides whether or not add a shader to the OpenGL pipeline before
        // passing the call on to GL11.glBindTexture.

        final String methodName = isObfuscated ? "func_179144_i" : "bindTexture";
        final String methodDescriptor = "(I)V"; // Takes one int, returns void.

        System.out.println("MALMO: Found GlStateManager, attempting to transform it");

        for (MethodNode method : node.methods)
        {
            if (method.name.equals(methodName) && method.desc.equals(methodDescriptor))
            {
                System.out.println("MALMO: Found GlStateManager.bindTexture() method, attempting to transform it");
                for (AbstractInsnNode instruction : method.instructions.toArray())
                {
                    if (instruction.getOpcode() == Opcodes.INVOKESTATIC)
                    {
                        MethodInsnNode visitMethodNode = (MethodInsnNode)instruction;
                        if (visitMethodNode.name.equals("glBindTexture"))
                        {
                            visitMethodNode.owner = "com/microsoft/Malmo/Utils/TextureHelper";
                            if (isObfuscated)
                            {
                                visitMethodNode.name = "bindTexture";
                            }
                            System.out.println("MALMO: Hooked into call to GlStateManager.bindTexture()");
                        }
                    }
                }
            }
        }
    }

}
