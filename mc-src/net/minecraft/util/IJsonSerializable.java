package net.minecraft.util;

import com.google.gson.JsonElement;

public interface IJsonSerializable
{
    void func_152753_a(JsonElement p_152753_1_);

    /**
     * Gets the JsonElement that can be serialized.
     */
    JsonElement getSerializableElement();
}