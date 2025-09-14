# THERMOnkey

## Projekt leírása
A **THERMOnkey** egy okos hőmérséklet-figyelő és szabályozó rendszer, amely vezeték nélküli kommunikációval képes adatokat továbbítani és megjeleníteni. A cél egy könnyen használható, megbízható és bővíthető megoldás létrehozása volt, amely otthoni vagy akár ipari környezetben is alkalmazható.

## Főbb jellemzők
- Matter protokoll támogatás, egyszerű integráció különböző okosotthon platformokkal (pl. Apple Home)
- Felhasználóbarát, 3D nyomtatott termosztát ház, beépített Adafruit 0.96" 160x80 Color TFT kijelzővel
- Vezeték nélküli, elemes termofej, alacsony energiafogyasztású üzemmódokkal
- Bluetooth Low Energy (BLE) kommunikáció a termosztát és a termofejek között
- Mechanikus áttétel a radiátor szelep mozgatásához

## Rendszer felépítése
- **Termosztát vezérlőegység:**
	- MG24 dev-kit alapú
	- Matter API integráció a Silabs Arduino core-ban
	- Kijelzőn pairing kód, aktuális és beállított hőmérséklet megjelenítése
- **Vezeték nélküli termofej:**
	- MG24 explorer-kit alapú
	- 4 db AA elem, ultra alacsony fogyasztás (EM4 mód)
	- Sleepy end device: csak időnként ébred, lekérdezi a termosztátot, majd visszaalszik
	- BLE kliensként csatlakozik a termosztáthoz
	- 1:4 áttétű fogaskerék rendszer, DC motoros meghajtás

## Kiemelt eredmények
- Stabil és megbízható Matter és BLE kommunikáció
- Energiahatékony működés, hosszú elem-élettartam
- Sikeres integráció Apple Home környezetbe
- Egyedi, 3D nyomtatott ház és mechanikai megoldások

## Csapattagok
- Csuta Krisztián
- Kovács Levente
- Mészáros Bálint

Budapesti Műszaki és Gazdaságtudományi Egyetem, Villamosmérnök BSc

## Dokumentáció
A részletes dokumentáció, ábrák és további információk a `doc` mappában találhatók.

## GitHub repository
[https://github.com/tuskeshatu/silabs-thermonkey](https://github.com/tuskeshatu/silabs-thermonkey)
