# Sheet fixtures

`hero.sprat` is written by hand in sprat-raw 2, the format `spratconvert --transform
raw` writes, with `hero.png` as its 32x32 atlas:

| Sprite | Atlas rect | Packing | Trim (l t r b) | Pivot |
|---|---|---|---|---|
| `hero/idle.png` | 0 0 12 20 (red) | upright | 2 4 2 0 | 8 24 |
| `props/bush.png` | 12 0 16 8 (green) | upright | none | 8 8 |
| `props/door.png` | 12 8 6 10 (brown) | turned 90° clockwise | 1 2 3 0 | 7 8 |
| `ui/panel.9.png` | 0 20 12 12 (blue) | upright, 4-pixel nine-slice | none | 0 0 |

The door's upright top row is yellow; packed, it is the region's right column, so
drawing it upright checks the rotation. `idle` is a one-frame animation of
`hero/idle.png`.
