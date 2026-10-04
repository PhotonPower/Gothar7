-- Darstellung der Dialoge (M10 Teil B): Kamera, Gesten. Die Engine liest diese Tabelle beim Laden der Skripte.
DialogPresentation = {
    camera = {
        side = 0.55,   -- m seitlich der Schulter des Zuhörers
        height = 1.62, -- m über seinen Füßen
        back = 0.9,    -- m hinter ihm
        look = 1.55,   -- m: Höhe des Blickpunkts am Sprecher
        blend = 0.25,  -- s, so weich folgt die Kamera beim Sprecherwechsel
    },
    -- Je Zeile eine Geste des Sprechers (figurens Set dlg, additiv gegen dlg/a_neutral).
    gestures = { "dlg/a_talk_1", "dlg/a_talk_2", "dlg/a_talk_3", "dlg/a_talk_4", "dlg/a_explain", "dlg/a_shrug" },
    -- Bei belegten Armen nur der Kopf (figuren: Armgesten auf solchen Haltungen sehen seltsam aus).
    head_gestures = { "dlg/a_nod", "dlg/a_shake_head" },
    -- Haltungen, in denen ein NPC zum Reden bleibt (sitzend am Boden); jede andere Tagesablauf-Animation endet.
    keep = { sit_ground = true },
}
