# Map quality pass: iteration log

Each iteration is one district per map rendered from the chase camera (8.5 m back, 18 degrees down, FOV 90) and a wider
beauty angle, in a Blender mirror of what `ASQArena` builds. Images: `<Map>_iter<N>_chase.png` / `_beauty.png`.

| Iter | Critique of the previous frame | What changed |
|---|---|---|
| 1 | (baseline) What commit f7e3b21 builds: random village houses, loose crates and barrels, red/blue/green kit colours, bare dark ground, weak light. Fails checklist items 1, 3, 4, 5, 6, 7, 8, 9. | Nothing: this is the "before". |
| 2 | Sparse ring of mismatched houses, loose props on the floor, no theme. | New district recipe (landmark, six flank fronts shoulder to shoulder, tall second row, two signature pieces at the shoulders, lantern tree, stalls in the yard wall, gate arch, bunting spans, tree line, fenced grave plots); 15 original Fair_ set pieces; per-map recolour of the kit atlases; every loose prop removed. |
| 3 | Iter 2 read as each theme but was murky: landmark sat on the floor, no light pools, flat ground, glow slots dark, signature pieces hidden behind the flank ring. | Landmark raised on `Fair_Podium`; district point lights; `M_SetGlow` windows/lanterns lit; ground v2 (paved plaza, promenade crescent, aprons, trails, contact shade, moss); signature pieces moved to the gate shoulders; stalls built into the yard wall; lantern arch / gear bridge as gates; bunting spans down the lane; fog and bloom retuned; moon moved front-left. |
| 4 | Glow blown out, lamps cast no shadows, harbour was dry land with nothing on the flats, bunting hung in the camera's face, green yews in the brass Clocktower, graveyard thin behind the left shoulder. | Lamp shadows, lower glow/bloom, contrast grade, stars; harbour gets sea water + wet sheen, 5 beached `Fair_Boat` hulks, mooring pilings round the quay, net racks; spans start further down the lane; Clocktower drops yews; third grave plot; gate tree moved off the lane. |
