# Handoff — EutherCommand, 2026-10-10

## Aktuellt resultat

Båda IR-sändarna är byggda. Användaren har bekräftat fungerande fysisk
Logitech-volymstyrning, senast via Samsung-kortet för den nya ESP8266-sändaren.
ESP8266 stod tillfälligt vid TV:n på bottenvåningen under provet. Nästa steg är
att hänga upp enheterna och kontrollera riktningen på deras slutliga platser.
Ingen ny flashning behövs för att flytta dem.

Detta är aktuell överlämning och ersätter tidigare anteckningar om att Samsung
saknar sändare eller att ESP8266:s första fysiska volymprov återstår.
Adresserna nedan är senast observerade, inte en ny kontroll av tillgänglighet.

## Noder och rum

| Mål i apparna | Placering | Hårdvara | IP / MAC | IR |
| --- | --- | --- | --- | --- |
| NEC · Bottenvåningen | Bottenvåningen | ESP32 | 192.168.32.248 / 94:e6:86:b5:01:fc | RX D27, TX D26 |
| Samsung · Övervåningen | En trappa upp; provad tillfälligt nere | ESP8266EX, 4 MB | 192.168.32.253 / 80:7d:3a:3e:20:fc | TX D2/GPIO4, ingen mottagare |

NEC-noden var inte nåbar vid senaste statuskontrollen. Det hindrar inte Samsung:
nodernas bakgrundsuppdateringar körs oberoende. Återkontrollera efter montering.
Reservera gärna IP-adresserna i DHCP. Knapparna skickar logiska mål-ID:n;
gatewayn väljer separat nodkonfiguration med pinnat certifikat och unik nyckel.
MAC-adresserna identifierar hårdvaran men är inte autentisering.

ESP8266-koppling: GPIO4 → 1 kΩ → transistorbas; 10 kΩ mellan bas och emitter;
emitter till GND; kollektor till IR-LED:ns katod. Cirka 5 V från USB går via två
270 Ω parallellt (135 Ω) till IR204C-A-L:s anod. Kontrollera transistorben mot
komponentens faktiska pinout. Se [kopplingsbild](../nodes/esp8266/docs/wiring-esp8266-tx.png)
och [ESP8266 README](../nodes/esp8266/README.md).
ESP32:s aktivitets-GPIO är fortsatt avstängt (-1), i väntan på identifierad styrbar LED.

## Appar, röst och snabb transport

- EutherVox **0.20.0-beta.4 / kod 68**, branch `feature/tv-apparater`.
- EutherBeam **0.1.0-alpha.15 / kod 15**, branch `main`.
- Båda har separata höj/sänk-knappar för NEC och Samsung. Uppdatera båda apparna:
  nya Beam använder en signaturskyddad broadcast med obligatoriskt mål; äldre Vox
  avvisar den i stället för att råka styra standardnoden.
- Inbäddad Beam följer Vox-temat, inklusive Classic och System Regis.
  Fristående Beam använder sin mörka/mintfärgade palett.
- Svenska röstalias omfattar höj/sänk volymen/ljudet och rumsnamn.
  NEC: bottenvåningen, nedervåningen, nere. Samsung: övervåningen, uppe,
  en trappa upp. Utan mål används senaste TV-val i samma session i upp till
  tio minuter, annars ställs följdfråga. Komplett fysiskt röstprov för båda rum
  är inte dokumenterat som verifierat.

Volym skickas som autentiserad EC1 UDP på port 7426, utan kvittensväntan,
återförsök eller kö. Minsta intervall är 160 ms. Epoch/sekvens/deadline ger
skydd mot gamla och dubbla paket. Samma Logitech NEC-adress 8 används på båda:
höj 0x1a, sänk 0x0e. En full signal följs av en NEC-repetition vid 110 ms;
hela trycket är cirka 121,81 ms. Denna korrigering gav faktisk volymändring,
i stället för att bara väcka volymvisningen. Se [FAST_IR.md](FAST_IR.md).

## Repon och drift

| Repo | Sökväg / branch | Senast pushad implementation och provdokumentation före denna handoff |
| --- | --- | --- |
| EutherCommand | `/run/media/nichlas/Kingston1TB/EutherCommand`, main | d2cb287 |
| EutherCommand-ESP8266 | `nodes/esp8266`, main; privat Git-submodul | 075fdc6 |
| EutherVox | `/home/nichlas/EutherVox`, feature/tv-apparater | d9813d5 |
| EutherBeam | `/home/nichlas/EutherBeam`, main | a1a80f4 |
| EutherOxide | server 192.168.32.186, `/home/nichlas/EutherOxide`, agent/euthernet-admin-boundary | ece3959 |

Vox-gatewayn kör som lokal användartjänst `euthervox-gateway.service` på .88;
`config.real-beta.example.toml` aktiverar målkonfigurationen. NEC använder
standardnoden, Samsung använder den privata filen
`nodes/esp8266/.local/client.json`. Offentlig autentiserad anslutning går genom
Caddy/EutherOxide på .186. Serverns tjänst heter `eutherhost.service`.

EutherOxides server-checkout är källa för deployment. Serverns Git-inloggning
fungerade inte vid senaste push; den exakta serverbranchen hämtades till lokala
EutherOxides `euther-server`-remote och pushades därifrån med fungerande lokal
Git-autentisering. Ändra inte serverhistoriken eller lokala arbetsfiler för detta.
Bevara redan befintliga ändringar i serverns `webview/build-info.ts` och lokala
EutherOxides `scripts/eutherhost-backup-status.py` och
`scripts/eutherhost-users-mirror.sh`.

Privata nodnycklar, klientkonfigurationer och firmware med inbyggda nycklar är
ignorerade och får inte checkas in. Ursprunglig ESP8266-flash och checksumma finns
i `.local/backups/esp8266-before-tx-20261010.*`. ESP8266 har manuell USB-återställning,
inte OTA eller IR-inlärning. Mottagare är beställda; lägg till stöd först när sådan finns.

## Publicerade APK:er

- Vox: https://apothictech.se/downloads/EutherVox-0.20.0-beta4-debug.apk
  (inloggning krävs).
  SHA-256: `e7730677b93d430fb9ce6100e302a878830653b1be419652b2171575afff9d21`.
- Beam: https://apothictech.se/downloads/EutherBeam-0.1.0-alpha15-debug.apk.
  SHA-256: `5bc91e7598ac7479675096999240e312e5a0c1ea74ab3bdc42cc94bf2b71726c`.

## Verifiering som redan är gjord

- ESP8266 byggd, flashad med hashkontroll och ansluten till hemmets Wi-Fi via mobil.
- Liveprov med TX avstängt: pinnad TLS, autentisering, UDP-validering,
  replay/deadline-skydd och ingen UDP-kvittens; två accepterade paket, noll sändningar.
  Testverktyget lämnar TX avstängt; explicit aktivering eller omstart återställer.
- 47 fokuserade gatewaytester godkända, inklusive målval och oberoende noduppdatering.
- Båda Android-apparnas build, lint och enhetstester godkända med Java 17.
- Emulator: åtta knapptryck (fyra per app) gick till rätt mål och riktning genom
  mocknoder. Classic, System Regis och Beam-tema visuellt kontrollerade.
- Serverns Rust/webbyggen och omstart lyckades. APK-hashar stämmer mellan lokal
  fil och server; publik Beam-nedladdning verifierad, anonym Vox ger förväntad 401.
- Användaren bekräftade därefter verklig volymändring på nya ESP8266-sändaren.
  Emulator- och nätverkstester räknas inte som fysisk IR-verifiering.

## Nästa session

1. Låt användaren montera och rikta sändarna, Samsung uppe och NEC nere.
2. Kontrollera respektive korts höj/sänk på slutlig plats, utan att skicka
   oannonserade volymserier. Bekräfta vilket rum som faktiskt reagerar.
3. Prova svenska röstkommandon med explicit TV/rum och därefter senaste mål.
4. Utöka inlärning och röstfraser via ESP32-mottagaren när användaren vill.
   ESP8266 är tills vidare en sändare med registrerade volymkommandon.

Gör inte om fungerande flashning eller provisioning enbart för en fysisk flytt.
Ingen fysisk enhetsstatus får hävdas utifrån enbart accepterad eller sänd IR.
