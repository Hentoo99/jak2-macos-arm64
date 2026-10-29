# Port iOS di Jak 2 — decisioni (fase 0) e prototipo del code model (fase 1)

Stato: **proposta** (AI-assisted), branch `ios`. Fase 1 (code model `page`) completata il 2026-10-29. Contesto e confronto con i port iOS di Jak 1
(ottsel, Citadel): `~/jak-ios/ANALISI.md`, fuori dal repo. Queste decisioni modificano il
piano di `docs/apple-aot-metal-playbook.md` dove indicato; il resto del playbook resta valido.

## ADR-IOS-001 — Funzioni GOAL come puntatori reali a testo firmato

**Decisione.** In modalità AOT, le funzioni GOAL restano puntatori `u32` relativi alla base EE.
Il codice GOAL firmato viene linkato in una sezione `__GOAL` (RX) posta subito dopo l'arena EE
(`__ARENA`, zerofill RW) nello stesso Mach-O. Così un offset `u32` da x22 raggiunge il codice.
Bridge C→GOAL e mips2c diventano stub firmati a dimensione fissa ("export bank"), con il target
in una tabella RW scritta una sola volta al boot.

**Sostituisce.** Il modello a descrittori `AOTF` + tabella per ID (playbook §2.3, AOT-002,
AOT-006 e AOT-007). Va quindi rivisto il percorso AOT di `make_function_from_c*` e
`call_goal` in `common/aot/AotExecution.*`.

**Motivazione.**
- Nessun resolver sulle chiamate.
- ABI invariata: `method-set!`, gli state handler e i `function` in dati statici funzionano
  senza modifiche.
- Modello già validato su dispositivo da ottsel (Jak 1, iPad M2, modalità strict su 518/518
  moduli).

**Conseguenze.**
- Lo spazio EE + `__GOAL` deve stare entro 4 GiB dalla base.
- Codice e dati statici di ogni oggetto finiscono in immagini diverse, quindi il codice deve
  raggiungere i dati con ±4 GiB (vedi ADR-IOS-002).

## ADR-IOS-002 — Code model ARM64 "page" (ADRP) in goalc

**Decisione.** goalc ha un code model ARM64 `page`, **default dal 2026-10-29**. Il vecchio
modello resta disponibile con `--arm64-code-model literal`:
- `ldr xN, [pc, #imm19]` diventa `adrp xN, lit@PAGE ; ldr xN, [xN, lit@PAGEOFF]`;
- `adr x17, lit` diventa `adrp x17, lit@PAGE ; add x17, x17, lit@PAGEOFF`.


**Perché funziona già con il JIT.** goalc risolve le coppie ADRP all'interno del segmento.
Il runtime alloca ogni segmento eseguibile allineato alla pagina (16 KiB, `kmalloc.cpp`), quindi
la distanza in pagine da 4 KiB tra codice e literal non cambia al caricamento. Non serve alcuna
modifica al formato `.o` né al linker runtime.

**Fase 2.** L'emitter AOT riconosce le coppie dall'opcode: `ADRP` seguito da `LDR` (unsigned
offset) o `ADD` (imm) con lo stesso registro. Le riemette come `@PAGE`/`@PAGEOFF` verso le
etichette dei dati nell'arena.

## ADR-IOS-003 — Grafica: OpenGL ES 3 via ANGLE prima di Metal nativo

**Decisione.** Il primo backend grafico iOS è il renderer GL esistente su GLES 3.0 tramite ANGLE
(Metal). Serve una nuova opzione `OPENGOAL_GRAPHICS_BACKEND=GLES`. Oggi `CMakeLists.txt:47-49`
impone METAL su iOS e andrà rilassato. Il backend Metal nativo (MTL-001…014) diventa un'
ottimizzazione successiva.

**Motivazione.**
- Si ereditano subito tutti i renderer di Jak 2: TextureAnimator, Warp, Shadow2, Blit,
  glow/distort.
- ottsel ha dimostrato GLES/ANGLE a 60 fps su iPad, con traduzione offline dei 92 shader
  (GLSL 410 → SPIR-V → ES 300).

## ADR-IOS-004 — Dati di gioco fuori dal bundle

**Decisione.**
- I dati estratti dalla ISO dell'utente non entrano mai nel repository né nel bundle dell'app.
- L'app riceve un pack (manifest + SHA256) importato in Application Support.
- Il loader firmato rifiuta gli oggetti con codice il cui SHA-256 non corrisponde al build.

---

## Censimento Jak 2 (build ARM64 attuale, code model `literal`)

| Voce | Valore |
|---|---|
| Oggetti v3 (`out/jak2-arm64/obj`) | 841 |
| Codice main / debug / top-level | 22,8 / 10,1 / 12,9 MiB |
| Dati statici main / debug / top-level | 7,7 / 1,7 / 1,1 MiB |
| Riferimenti codice→statici | 289 874 `ldr` literal + 27 934 `adr` |
| Costo del code model `page` | +1 istruzione per riferimento ≈ +1,2 MiB di codice |
| Bridge C→GOAL registrati per nome (jak2 + common) | ~97 diretti (`make_*function_symbol_from_c`), da censire esattamente al boot |
| Registrazioni mips2c (`game/mips2c/jak2_functions`, 27 file) | 138 |

Per confronto, Jak 1 in ottsel ha 19,5 MiB di testo firmato e 2,4 MiB di statici. Jak 2 ha circa
46 MiB di codice, o circa 36 MiB escludendo il segmento debug in release.

Il codice top-level (12,9 MiB) sul PS2 viene eseguito una volta e scartato. In un'immagine firmata
resta in `__TEXT`. È un'ottimizzazione possibile per la fase 2.

## Prototipo del code model `page` (fase 1)

Modifiche:
- `goalc/compiler/IR.cpp`: `emit_arm64_literal`, `emit_arm64_literal_address`.
- `goalc/emitter/IGenARM64.*`: `adrp_gpr64`, `load64_gpr64_lo12`, `add_gpr64_lo12`.
- `goalc/emitter/ObjectGenerator.*`: flag e classificazione per opcode.
- `goalc/emitter/Relocation.*`: `ADRP` corretto per target non allineati e all'indietro; nuovo
  `Arm64Ldr64Lo12`.
- Plumbing: `goalc/main.cpp`, `Compiler`, `CodeGenerator`.

Strumento di verifica: `scripts/arm64_code_model_audit.py`. Decodifica gli oggetti v3 e conta e
valida i riferimenti. Con `--require-page` fallisce se resta un riferimento literal; con
`--compare` confronta due build.

Risultati:

1. **Default invariato.** `types-h.o` e il kernel ricompilati con `--arm64-code-model literal`
   sono identici byte per byte al build precedente (`cmp`).
2. **`gcommon.o` in modalità page.** 407 `ldr` + 66 `adr` diventano 407 `adrp+ldr` + 66
   `adrp+add`. Tutti i 473 riferimenti puntano agli stessi offset statici nello stesso ordine
   (`--compare`, segmenti main/debug/top-level).
3. **Kernel completo in modalità page** (`KERNEL.CGO`: gcommon, gstring-h, gkernel-h, gkernel,
   pskernel, gstring, dgo-h, gstate). Contiene 0 `ldr` literal / `adr` e 1979 `adrp+ldr` +
   241 `adrp+add`.
4. **Runtime JIT con kernel in modalità page.** `gk --no-display -- -boot -fakeiso -nosound` per
   60 s arriva a `play-boot`, al titolo e carica 4 livelli (title, ctywide, lwidea, ctysluma).
   Il log normalizzato è identico al run con kernel `literal`; l'unica differenza è l'ordine di
   un warning tra thread.
5. **Test.** 301 test ARM64/emitter/AOT/kernel verdi, più quelli nuovi:
   - `RelocationModel.AdrpPage21UnalignedTargets`;
   - `RelocationModel.Ldr64Lo12`;
   - `ObjectGeneratorARM64.PageRelativeStaticReference`.

**Gate della fase 1 superato (2026-10-29):**
6. **Build completa in page.** `(make-group "all-code" :force #t)` + `(mi)`: 841 oggetti e 93
   CGO/DGO in circa 14 s. `GAME.CGO` cresce di circa 430 KB.
7. **Audit.** `arm64_code_model_audit.py --require-page out/jak2-arm64/obj` su 841 oggetti:
   0 `ldr` literal / `adr`, 290 281 `adrp+ldr` e 28 000 `adrp+add`.
8. **Headless.** 90 s headless senza fault: titolo e 4 livelli.
9. **Sessione con finestra.** Fatta dall'utente con `task boot-game` (menu, Haven City,
   missione, salvataggio): funziona.
10. **Default.** `page` diventa il default (`goalc/main.cpp`, `Compiler::m_arm64_page_relative`).
    goalc senza opzioni produce 840/840 oggetti identici alla build page. 302 test verdi.

Per tornare al vecchio modello basta ricompilare con `--arm64-code-model literal`
(`all-code` forzato + `(mi)`).

**Prossimo passo: fase 2.** Emitter da oggetto a `.S` firmato, arena `__ARENA`, loader firmato
ed export bank per Jak 2. Primo traguardo: Jak 2 AOT su macOS senza `allow-jit`.

Nota operativa: i run headless vanno lanciati con `--config-path <dir temporanea>`. Altrimenti
`gk` riscrive `pc-settings.gc` dell'utente con la risoluzione di ripiego (320×240).

---

## Fase 2a — emitter di moduli firmati (2026-10-29)

Componenti:
- `goalc/aot/ObjectFileV3.*`: parser validato degli oggetti v3 ARM64.
- `goalc/aot/SignedModuleEmitter.*` + `jak2-aot-emit`: genera `modules/*.S` (codice in
  `__GOAL,__text`, template dei dati in `__TEXT,__const`), `arena-layout.S` (arena EE come
  `__ARENA` zerofill da 256 MiB, con un'etichetta dati per segmento a offset fisso
  da `0xa000000`), `module-index.c` (tabella per il loader, `common/aot/SignedModuleIndex.h`) e
  `manifest.json` (link record solo-dati per il loader).
- `common/util/sha256.*`: identità dei moduli.

Come l'emitter tratta il codice:
- Ogni coppia ADRP diventa `adrp/ldr|add …@PAGE/@PAGEOFF` verso l'etichetta dati del segmento.
- Ogni type tag `function` diventa la costante passata con `--function-type`.
- Rifiuta qualsiasi link che scriva nel codice e qualsiasi riferimento literal/ADR.

Verifica: `scripts/aot_signed_roundtrip.sh` linka l'output in un eseguibile reale (ASLR attivo)
e `test/aot/signed_module_roundtrip.cpp` confronta il testo linkato con gli oggetti originali.

| Insieme | Moduli | Codice | Coppie ADRP | Regioni dati | Esito |
|---|---|---|---|---|---|
| Kernel | 8 | 311 KB | 2 220 | 15 | 0 errori |
| Gioco intero | 841 | 48,9 MB | 318 281 | 2 108 | 0 errori, 17 s |

- Il testo firmato finisce ad arena + 0x12ea4c90, quindi è raggiungibile con offset GOAL `u32`.
  Il linker mette `__GOAL` subito dopo `__ARENA` se `arena-layout.o` precede i moduli.
- Controprova: tre corruzioni volute dell'assembly (tag, istruzione, addendo ADRP) vengono
  rilevate tutte e tre.
- Test gtest: `Sha256.*`, `AotSignedModule.*`.

### Nodi aperti per la fase 2b (loader runtime)

1. **Tipo `function`.** È il quarto oggetto allocato nell'heap globale da `InitHeapAndSymbol`,
   quindi deterministico per una data build del runtime. Il loader deve verificare
   `FIX_SYM_FUNCTION_TYPE == goal_aot_function_type` prima di eseguire codice firmato.
2. **Oggetti condivisi tra DGO.** 122 degli 840 oggetti stanno in più DGO, fino a 7: `bot`,
   `hover-nav-*`, `crimson-guard-level`…
   - Con una sola regione dati fissa per modulo, due livelli caricati insieme che contengono
     lo stesso oggetto condividono i suoi statici.
   - Proposta: conteggio dei riferimenti. Il template viene ricopiato solo al primo
     caricamento attivo; al secondo i link vengono riapplicati e il top-level rieseguito, senza
     azzerare i dati.
   - Da validare in gioco. L'alternativa è duplicare le istanze di codice degli oggetti condivisi.
3. **Bridge C→GOAL e mips2c.** Diventano export bank firmati, oggi trampolini JIT:
   `make_function_from_c*` e `LinkedFunctionTable::reg`.
4. **Integrazione.** Circa 49 MB di testo e 244 MB di assembly generato vanno compilati e
   linkati in `gk` per un target macOS AOT, senza `allow-jit`.

---

## Fase 2b — Jak 2 AOT su macOS senza JIT (2026-10-29)

`scripts/build_jak2_aot_macos.sh` produce `build-arm64-aot/game/gk.app`. La configurazione è
`OPENGOAL_EXECUTION_MODE=AOT` con `OPENGL`, e l'immagine generata sta in `out/aot/jak2/macos-arm64`.

### Cosa cambia nel runtime (solo con `OG_EXECUTION_MODE_AOT`)

- **Memoria EE.** È il segmento `__ARENA` dell'eseguibile (`runtime.cpp`). È zerofill, quindi
  non viene azzerata con `memset`.
- **`call_goal`.** Salta direttamente alla funzione firmata, come in modalità dinamica. Il
  dispatch dei descrittori `AOTF` non è più usato (ADR-IOS-001).
- **Bridge C→GOAL e mips2c.** Usano stub firmati presi dagli export bank (`export-banks.S`
  generato da `jak2-aot-emit`):
  - `make_function_from_c*` e `LinkedFunctionTable::reg` passano da `signed_aot::bind_export`;
  - `nothing`/`zero` usano stub fissi.
- **Loader `link_control::jak2_work_v3_signed`** (`jak2/klink.cpp`):
  - trova il modulo per nome;
  - verifica lo SHA-256 dell'oggetto, ripristinando il magic "GOAL" che `jak2_work`
    sovrascrive con il tag del link block;
  - copia i template nell'arena e applica i link solo come dati.

  La modalità dei link dei simboli viene letta dal template, non dall'arena. I dati statici
  vengono reimpostati, a meno che il modulo sia vivo in un altro heap (vedi "Nodi aperti").
- **Tipo `function`.** In AOT `InitSymbolTableAndBaseTypes` gira prima di `init_output`, che
  mette i suoi buffer nell'heap globale solo senza debug. Così il tipo sta a **0x16fde4** in
  ogni modalità di boot. Una discrepanza è un errore fatale che indica il valore giusto.
- **`END_OF_MEMORY`.** Vale 0x40000000 su ARM64, sia in GOAL (`gcommon.gc`) sia in C
  (`memory_layout.h`, usato da print/inspect/controlli sui tipi). Statici e codice firmato
  stanno sopra i 128 MiB.

### Verifica (headless, configurazione separata)

| Prova | Esito |
|---|---|
| Boot senza `-debug`, 60 s | titolo e 4 livelli (title, ctywide, lwidea, ctysluma); log identico al JIT |
| Boot con `-debug`, 60 s | prison, ldjakbrn, forexita; identico al JIT nella stessa modalità |
| `mmap(MAP_JIT)` / `mmap(PROT_EXEC)` / `mprotect(PROT_EXEC)` | **0 / 0 / 0** in AOT; controprova JIT: 1 / 0 / 1183 |
| `scripts/aot_signed_roundtrip.sh` | 840 moduli + 704 stub, 0 errori |
| Test di regressione (build JIT) | 221 verdi |

I conteggi di memoria eseguibile vengono da un'interposizione di `mmap`/`mprotect` con
`DYLD_INSERT_LIBRARIES`.

Nota: firmare con il runtime rafforzato senza `allow-jit` **non** prova niente su macOS. Con
firma ad-hoc `mmap(MAP_JIT)` RWX viene rifiutato, ma RW più `mprotect` RX è permesso, tanto che
anche la build JIT gira (controprova fatta). L'ambiente vincolante sarà iOS; fino ad allora la
prova è l'audit per interposizione.

**Sessione con finestra sulla build AOT** (2026-10-29), fatta dall'utente: menu, Haven City,
cambi di livello e missione; funziona.

### Nodi aperti

1. **Oggetti condivisi tra DGO (122).** Nella sessione utente non sono emersi problemi. L'euristica attuale condivide gli statici quando un
   modulo viene caricato in un heap diverso. Se un livello viene scaricato da uno slot e
   ricaricato in un altro, i suoi statici non vengono reimpostati. Va verificato in gioco, con
   sessioni lunghe.
2. **REPL/listener.** In AOT il codice inviato da goalc viene rifiutato perché non ha un modulo
   firmato. È voluto.
3. **Prossimo passo.** Target iOS (ANGLE/GLES, shell, pack dati), dove il divieto di JIT è
   imposto dal sistema.

---

## Fase 3 (in corso) — grafica OpenGL ES 3 / ANGLE su macOS (2026-10-29)

`OPENGOAL_GRAPHICS_BACKEND=GLES` (ADR-IOS-003): lo stesso renderer OpenGL su OpenGL ES 3.0 di
ANGLE (Metal). Si configura con `-DOPENGOAL_ANGLE_DIR=<ANGLE prebuilt>`, che sta fuori dal repo;
la build di prova è `build-arm64-gles` (JIT + GLES).

Componenti:
- `game/graphics/gles_compat.{h,cpp}` (da ottsel, ISC). Viene incluso forzatamente nei sorgenti
  grafici e copre texture 1D → 2D piegate in righe da 1024, formati `8_8_8_8_REV`,
  `glMultiDrawElements`, primitive restart a indice fisso e le chiamate solo-desktop.
- `scripts/translate_gl_shaders_es.py` (da ottsel, costanti di Jak 2). Traduce GLSL 410 in
  ES 300 via glslang e SPIRV-Cross in `out/gles-shaders` durante la build: 92 shader su 92.
- `opengl.cpp`: finestra SDL Metal, contesto EGL di ANGLE, `eglSwapBuffers`, ImGui sul backend
  ES3. L'output `KHR_debug` di ANGLE finisce nel log.
- ANGLE (dylib con nomi `@rpath`) copiato nel bundle `Frameworks/`.

Correzioni verificate con l'output di debug di ANGLE e con screenshot A/B rispetto al GL desktop:
1. **Merc2.** Bone da UBO a texture RGBA32F con `bone_base` per draw (da ottsel). Su ANGLE gli UBO
   per draw producevano esplosioni di geometria. Vale per tutti i backend: sul desktop il
   risultato è identico.
2. **`FramebufferTexturePair`.** In ES il draw buffer 0 deve essere `COLOR_ATTACHMENT0`.
3. **`GlowRenderer`.** Al ridimensionamento la texture di sonda viene ricreata
   `DEPTH24_STENCIL8`, come all'inizializzazione. Prima era `GL_DEPTH_COMPONENT` e ES rifiutava
   il blit di profondità.

Strumenti di sviluppo, sempre disponibili e inattivi senza variabili d'ambiente:
- `OPENGOAL_SCREENSHOT_FRAMES="1196,1500"`: salva `screenshots/frame-<n>.png`;
- `OPENGOAL_DISABLE_RENDERERS="tie,merc"`: disattiva renderer dall'avvio;
- `OPENGOAL_DISABLE_RENDERERS_AT="1200:tie-l"`: li disattiva da un frame preciso, così due
  screenshot ravvicinati confrontano la stessa scena.

**Ancora aperti in GLES** (titolo di Haven City; assenti sul GL desktop):
- un triangolo nero su un cartellone, prodotto da un **TIE normale**: sparisce spegnendo
  `tie-lN-tfrag` e resta spegnendo `etie`. Si è verificato che non dipende da primitive restart
  (attivo), dal percorso multidraw (identico in single-draw) né dai fan di TextureAnimator;
- sottili strisce colorate, che restano spegnendo shadow, sprite, merc, tie e generic uno alla
  volta. Non è ancora attribuito;
- il cielo diventa ciano a tratti e un'insegna al neon è deformata, probabilmente per
  TextureAnimator.

Le prove con Shadow2 a liste di triangoli e con i fan di TextureAnimator convertiti non hanno
cambiato nulla e sono state annullate.

---

## Fase 4 (avvio) — Jak 2 nel Simulatore iOS (2026-10-29)

`scripts/run_jak2_ios_simulator.sh` compila `build-ios-sim`: iOS 17+, simulatore arm64, AOT con
GLES, ANGLE `iphonesimulator-arm64` e linking statico. Poi firma ad-hoc, installa e avvia l'app
sul simulatore (default "iPad Pro 11-inch (M5)"). Per ora i dati del gioco si leggono dal repo
con `--proj-path`.

Cosa è servito per iOS:
- `game/ios/Info.plist.in`: app iOS a schermo intero in landscape, iPhone e iPad, Files
  sharing.
- I framework ANGLE copiati in `gk.app/Frameworks`, con rpath `@executable_path/Frameworks`.
- `main.cpp` include `SDL_main.h` su iOS: SDL avvia UIKit e poi chiama il `main` del gioco.
- **SDL senza OpenGL/OpenGLES su iOS.** Altrimenti SDL linka il framework OpenGLES di Apple e i
  suoi simboli `gl*` oscurano quelli di ANGLE: ImGui riceveva `glGetString` NULL e andava in
  crash.
- ImGui OpenGL3 su iOS tramite `imgui_impl_opengl3_angle.cpp`, che usa gli header GLES3 di
  ANGLE invece di quelli OpenGLES di Apple.
- `discord_jak2.cpp` (tabelle dei livelli) e `tools/` restano nelle build GLES iOS, perché li
  usa la GUI di debug del renderer.
- `signed_aot::fatal` scrive anche nel log del gioco, perché su iOS non c'è una console.

Risultato sul simulatore iPad Pro 11" (M5), iOS 27:
- **Jak 2 si avvia e arriva al titolo** ("Press the Start Button", Haven City), con
  title/ctywide/lwidea/ctysluma caricati.
- **Audit della memoria eseguibile nel processo iOS** (interposizione di `mmap`/`mprotect`,
  compilata per il simulatore): `mmap(PROT_EXEC)=0`, `mmap(MAP_JIT)=0`, `mprotect(PROT_EXEC)=0`.
  L'SDK iOS non espone nemmeno `pthread_jit_write_protect_np`.
- La compilazione degli shader da parte di ANGLE all'avvio richiede circa 30 s nel simulatore.
  Va misurata sul dispositivo; se serve, si aggiunge una cache dei binari dei programmi.

Prossimi passi:
- pack dati nella sandbox, al posto di `--proj-path`;
- controlli touch e gamepad;
- audio (oggi stub muto);
- dispositivo fisico (iPad Air 5, iPhone);
- i difetti grafici GLES ancora aperti.
