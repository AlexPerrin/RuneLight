package net.runelite.host;

import java.util.Arrays;

import net.runelite.api.kit.KitType;

/**
 * PlayerComposition snapshot from the game's appearance object (Player +0x728, docs/offsets.md
 * "Player appearance"). Equipment ids use RuneLite's encoding: 0 empty, kit + 256, item + 2048.
 * Colour/texture overrides are the appearance's per-slot obj customisations, read live (Native.playerOverrides,
 * game thread); createColorTextureOverride keeps a plugin's override in this snapshot only (the game's model
 * doesn't change), and removeColorTextureOverride drops such an override.
 */
final class AndroidPlayerComposition implements net.runelite.api.PlayerComposition {
    private final int index;
    private final int[] equipment, kits, colors;
    private final int gender;
    private int npcId;
    private final net.runelite.api.ColorTextureOverride[] created = new net.runelite.api.ColorTextureOverride[12];

    AndroidPlayerComposition(int index, int[] equipment, int[] kits, int[] colors, int gender, int npcId) {
        this.index = index;
        this.equipment = equipment;
        this.kits = kits;
        this.colors = colors;
        this.gender = gender;
        this.npcId = npcId;
    }

    boolean same(int[] e, int[] k, int[] c, int g, int n) {
        return g == gender && n == npcId && Arrays.equals(e, equipment) && Arrays.equals(k, kits) && Arrays.equals(c, colors);
    }

    @Override public boolean isFemale() { return gender == 1; }
    @Override public int getGender() { return gender; }
    @Override public int[] getColors() { return colors; }
    @Override public int[] getEquipmentIds() { return equipment; }

    @Override
    public int getEquipmentId(KitType type) {
        int id = equipment[type.getIndex()];
        return id >= ITEM_OFFSET ? id - ITEM_OFFSET : -1;
    }

    @Override
    public int getKitId(KitType type) {
        int id = equipment[type.getIndex()];
        return id >= KIT_OFFSET && id < ITEM_OFFSET ? id - KIT_OFFSET : -1;
    }

    /**
     * As desktop: the composition's equipment ids, colours and transformed NPC id (a plugin's edits of
     * getEquipmentIds()/getColors() included) into the player's appearance, and its model hash
     * recomputed, so the next draw builds the model from them.
     */
    @Override public void setHash() {
        int[] e = equipment.clone(), c = colors.clone();
        int n = npcId;
        if (Native.isClientThread()) Native.playerSetHash(index, e, c, n);
        else Host.invoke(() -> Native.playerSetHash(index, e, c, n));
    }
    @Override public int getTransformedNpcId() { return npcId; }
    @Override public void setTransformedNpcId(int id) { npcId = id; }
    /** Per slot (KitType index): a plugin's override, else the game's customisation; null when there are none. */
    @Override public net.runelite.api.ColorTextureOverride[] getColorTextureOverrides() {
        short[][][] g = Native.isClientThread() ? Native.playerOverrides(index) : null;
        net.runelite.api.ColorTextureOverride[] out = new net.runelite.api.ColorTextureOverride[12];
        boolean any = false;
        for (int i = 0; i < 12; i++) {
            out[i] = created[i] != null ? created[i] : g != null && g[i] != null ? new TextureOverride(g[i][0], g[i][1]) : null;
            any |= out[i] != null;
        }
        return any ? out : null;
    }

    @Override public net.runelite.api.ColorTextureOverride getColorTextureOverride(KitType type) {
        net.runelite.api.ColorTextureOverride[] all = getColorTextureOverrides();
        return all == null ? null : all[type.getIndex()];
    }

    @Override public net.runelite.api.ColorTextureOverride createColorTextureOverride(KitType type, int n) {
        return created[type.getIndex()] = new TextureOverride(new short[n], new short[n]);
    }

    @Override public void removeColorTextureOverride(KitType type) { created[type.getIndex()] = null; }

    /** Recolour-to / retexture-to values (the "from" values are the item's own replace arrays). */
    static final class TextureOverride implements net.runelite.api.ColorTextureOverride {
        private final short[] colors, textures;
        TextureOverride(short[] colors, short[] textures) { this.colors = colors; this.textures = textures; }
        @Override public short[] getColorToReplaceWith() { return colors; }
        @Override public short[] getTextureToReplaceWith() { return textures; }
        @Override public String toString() { return "ColorTextureOverride[colors " + Arrays.toString(colors) + " textures " + Arrays.toString(textures) + "]"; }
    }

    @Override
    public String toString() {
        return "PlayerComposition[equipment=" + Arrays.toString(equipment) + " gender=" + gender + " npc=" + npcId + "]";
    }
}
