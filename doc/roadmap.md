# vm1 — Roadmap (Контракт проекта)

> **Контракт — закон.** Этот файл определяет фазы, API и критерии приёмки. Изменения — только через Architect. Coder не меняет сигнатуры без апрува. Версия: 2.0 (Demo 2 — Positional Addressing, Pairwise-Stable).

## 1. Цель

Демо позиционной адресации: копирование `n` бит `src → dst` параллельно для множества интервалов **без арифметики**, только лексикографическое сравнение `>` ключей внутри sorting network.

- Интервал — открытый `(low, high)`, копируются биты `[low+1, high-1]`.
- Каждой позиции соответствует `subspace[i].key = {low, high, partner, sort, origin}`.
- Только операция над ключами — `>` на 16-бит `sort`.

## 2. Архитектура (контракт модулей)

| Модуль | Файл | Ответственность | Публичный API |
|--------|------|------------------|---------------|
| **Machine** | `src/machine.h` / `src/machine.c` | 2-фазный движок адресации, память `space`/`subspace`, сеть `fabric`, каналы | `machine`, `subspace`, `network`, `SPACE_SIZE`, `LIFE`, `CHANNEL_IN/OUT`, `ceil_log2_u32()`, `set_bit_u8()` |
| **Network generator** | `src/network.c` | Генерация pairwise stable сети | `main() → sorter.txt` формат `a b\n\n` |
| **Loader** | `tools/loader.c` | Упаковка `.vm1` → `LDR1` (`LDR1 + u16 count + u16 base + payload`) | CLI `loader --in-prog FILE --out FILE [--dst-word-base N] [--tail-bytes FILE]` |
| **Compile** | `tools/compile.c` | `n dst src` → `4` слова `dst_low dst_high src_low src_high` | CLI `compile --in-copy3 FILE --out-vm1 FILE` |
| **Tests** | `tests/test_machine.c`, `test_network_unit.c`, `test_loader.c`, `test_compile.c`, `test_network.c`, `test_channel.c` | Unit + интеграция | `make check` |
| **Doc** | `doc/README.md`, `doc/hardware.md`, `doc/roadmap.md` | Спека | — |

### 2.1 Инварианты памяти

- `SPACE_SIZE = 65520` бит = 8190 байт. `SPACE_WORDS = SPACE_SIZE/8/sizeof(idx) ≈ 4095`.
- `PROCESSOR_N = 128` слотов × `EDGES(2)` × `WAYS(2)` = 512 слов лимит программы.
- `CHANNEL_IN = SPACE_SIZE/8 -1` (последний байт), `CHANNEL_OUT = SPACE_SIZE/8 -2`.
- `LIFE = 0x10` циклов макс. `INDEX_SIZE = ceil_log2(SPACE_SIZE) = 16`.

### 2.2 Сеть

- Pairwise stable (не Batcher odd-even). Для `SPACE_SIZE` не степень 2: строится на `next_pow2`, фильтруется `a,b < SPACE_SIZE`, репакуется жадно по `ready[]`.
- Текст `sorter.txt`: `a b` десятичные, пробел, `\n`; пустая строка между слоями. Гарантия `a<b`, `a != b`, без петель.

### 2.3 I/O

- `stdin` — поток байт. Опционально префикс `LDR1` в начале. Если первых 4 байта != `LDR1`, они трактуются как данные `CHANNEL_IN` (pushback-буфер, не теряются на pipe).
- `stdout` — поток байт `CHANNEL_OUT` по одному за цикл.

## 3. Фазы выполнения

```c
allocate_machine(argc, argv) // load sorter, init origin, install_loader
maybe_load_program_from_stdin() // LDR1?
while (life-- && channel_read()==0) {
  stage1_init();  run_network(stage1_pair);
  stage2_init();  run_network(stage2_pair);
  channel_write();
}
```

### 3.1 Stage1 — mark

- `sync_space_and_subspace()` — dst (`role==1`) пишет `value` в `space`, иначе читает.
- `reset_stage1_state()` — `active=0, role=0, sort=origin`.
- `seed_intervals_from_program()` — из `space[0..512)` по 4 слова → два дескриптора `subspace[base_word]` (dst) и `[base_word+1]` (src).
- `stage1_pair(a,b)` — распространяет `active` интервал вправо/влево, ставит `sort=partner` где `pos ∈ (low,high)`.

### 3.2 Stage2 — transpose & move

- `stage2_init()` — `partner = origin` везде.
- `stage2_pair(a,b)` — если `sort[a] > sort[b]`: `swap(sort,partner)`, затем `move_value_and_role`.

## 4. План по фазам (Roadmap этапов)

### Этап 0 — Инфраструктура (DONE)

- [x] `src/machine.h` типы, `Makefile` (`all/sorter/check/clean`)
- [x] `.clang-format` LLVM, `src/loader.h` 4 слова
- [x] `doc/README.md`, `doc/hardware.md`

### Этап 1 — Сеть (DONE)

- [x] `src/network.c` pairwise `gen_pairwise_pow2()` + `seq_pack()` + `next_pow2()`
- [x] `make sorter` → `sorter.txt` 3_997_174 пар, 136 слоёв
- Критерий: `tests/test_network` 4 кейса (inc/dec/pseudo/rnd) → sorted

### Этап 2 — Машина (DONE, исправлен)

- [x] `src/machine.c` 2 фазы, `channel_read/write`, `LDR1` с pushback (фикс pipe-loss)
- [x] `ceil_log2_u32`, `SETBIT` → `set_bit_u8`, `read_pair` строгий, `wire_used` переименование
- Критерий: `echo.asm` через `compile→loader→vm1` → `TAP`/`compat` 12/12

### Этап 3 — Инструменты (DONE)

- [x] `tools/compile.c` `encode_vm1(n,dst,src)`
- [x] `tools/loader.c` `LDR1` пакер
- Критерий: `examples/echo.asm` компилируется, `loader --tail-bytes` добавляет хвост

### Этап 4 — Тесты (DONE)

- [x] `tests/test_machine.c` (149 CHECK) — unit-тесты на каждую функцию `machine.c`: `inside_open`, `is_wire_reused`/`wire_used_reset`, `layer_reserve`, `network_reserve`, `network_add_layer`, `network_last_layer`, `network_push_pair`, `network_free`, `read_pair`, `trim_empty_tail_layer`, `sorter_to_network`, `buffered_getc`/`buffered_pushback`/`buffered_read_u8`, `channel_read`/`channel_write` (mock stdin/stdout), `stream_read_u8`/`stream_read_u16le`, `read_word`/`write_word` (+OOB через fork), `maybe_load_program_from_stdin` (полный LDR1 + pushback + truncated), `parse_args_or_die` (fork), `allocate_machine`, `init_origin_keys`, `install_loader`, `sync_space_and_subspace`, `reset_stage1_state`, `program_word_limit`, `seed_interval_pair`, `seed_intervals_from_program`, `stage1_init`, `copy_pos`/`move_pos`, `stage1_from_left`/`stage1_from_right` (partner сохраняется до move), `stage1_pair`, `stage2_init`, `swap_sort_and_partner`, `move_value_and_role`, `stage2_pair`, `run_network`, `run_cycle`, `run_channels` (LDR1 echo end-to-end), `ceil_log2_u32`, `set_bit_u8`
- [x] `tests/test_network_unit.c` (43 CHECK) — unit-тесты на каждую функцию `src/network.c`: `parse_u32`, `is_pow2`, `next_pow2`, `seq_add`/`seq_free`, `net_reserve`/`net_ensure_layer`/`net_add`/`net_free`, `net_print`, `gen_pairwise_pow2` (все 24 перестановки n=4 + инвариант a<b), `seq_pack` (сохранение пар, отсутствие конфликтов wire), `die` (fork)
- [x] `tests/test_loader.c` (36 CHECK) — `parse_u16`, `parse_u16_strict`, `trim`, `parse_vm1_file` (включая пустой файл, 3/5 полей, OOB), `write_ldr1` (magic/count/base/payload LE, tail, пустая программа), `die`/`die2` (fork)
- [x] `tests/test_compile.c` (24 CHECK) — `parse_long`, `encode_vm1` (границы, переполнения, null), `trim`, CLI round-trip `copy3→vm1`, `die`/`die2` (fork)
- [x] `tests/test_network.c` — 256-проекция сорт-сети
- [x] `tests/test_channel.c` — compat echo/chain/dual (12 комбинаций), gating; hardened `write`/`dup2`/`fork`
- Критерий: `make check` зелёный (6/6 gating), `-Werror` чист, ASAN `-O0` линкуется, Valgrind 0 leaks

### Этап 5 — Примеры и рантайм (PARTIAL)

- [x] `examples/echo.asm` — 1 инструкция `8 65504 65512`
- [ ] `examples/sort256_runtime.vm1` — сейчас заглушка `0 0 0 0`. Цель: программа в адресном пространстве, сортирующая 256 байт из `CHANNEL_IN` и выводящая `256 elements sorted.\nOK\n` в `CHANNEL_OUT`. Блок для следующего спринта (не блокирует Demo 2).

### Этап 6 — Железо (FUTURE)

- Спека `doc/hardware.md`: 33M тр без ROM, 798M с ROM, FSM `IDLE→CHANNEL_READ→STAGE1_INIT→STAGE1_RUN→STAGE2_INIT→STAGE2_RUN→CHANNEL_WRITE→DONE`
- Следующий шаг: small model + pipeline по слоям.

## 5. Контракты CLI

```
vm1 sorter.txt < in.bin > out.bin
sort [SPACE_SIZE] > sorter.txt
compile --in-copy3 in.asm --out-vm1 out.vm1
loader --in-prog out.vm1 --out prog.ldrpkt [--dst-word-base N] [--tail-bytes tail.bin]
make check   # gating: test_machine + test_network_unit + test_loader + test_compile + test_network + test_channel
```

Коды возврата: `0` успех, `1` ошибка протокола/валидации, `2` usage.

## 6. Критерии приёмки (Definition of Done)

- Сборка без `Wall/Wextra/Werror`.
- `src/machine.c` — Valgrind 0 leaks, `ceil_log2` корректен, pipe-loss отсутствует, `read_pair` строгий.
- Покрытие: каждая функция `machine.c` имеет ≥1 unit-тест + граничные случаи (пустой интервал, `low>high`, `LIFE` истощение, `SPACE_SIZE` границы, LDR1 truncated).
- `doc/roadmap.md` актуален, `doc/audit.md` → `[APPROVED]`.
- `make check` проходит без `|| echo`.

## 7. Риски

- Pushback-буфер ограничен 8 байт — достаточно для 4-байтного lookahead, но не для произвольного `unget`.
- `LDR1` на pipe требует atomic read 4 байт — частичный read (<4) корректно пушится назад.
- Сеть 4M пар → эмулятор ~8M ops/байт — CPU-bound, но в рамках `LIFE=16`.

---
*Контракт зафиксирован 2026-09-05. Изменения — PR с подписью Architect.*
