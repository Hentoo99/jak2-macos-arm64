# Port iOS di Jak 2 — decisioni (fase 0) e prototipo del code model (fase 1)

Stato: **proposta** (AI-assisted), branch `ios`. Contesto e confronto con i port iOS di Jak 1
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

**Decisione.** goalc ha un secondo code model ARM64, selezionato con
`--arm64-code-model page`:
- `ldr xN, [pc, #imm19]` diventa `adrp xN, lit@PAGE ; ldr xN, [xN, lit@PAGEOFF]`;
- `adr x17, lit` diventa `adrp x17, lit@PAGE ; add x17, x17, lit@PAGEOFF`.

Il default resta `literal` finché il gioco completo non è validato in `page`.

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

**Prossimo gate della fase 1 (build lunga, da confermare):**
1. `(mi)` completo con `--arm64-code-model page`;
2. `arm64_code_model_audit.py --require-page out/jak2-arm64/obj`;
3. una partita con finestra (menu, Haven City, una missione).
4. Se tutto passa, `page` diventa il default ARM64.

Nota operativa: i run headless vanno lanciati con `--config-path <dir temporanea>`. Altrimenti
`gk` riscrive `pc-settings.gc` dell'utente con la risoluzione di ripiego (320×240).
