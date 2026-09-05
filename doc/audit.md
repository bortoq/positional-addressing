# Аудит vm1 — 2026-09-05 (полный)

**Аудитор:** Senior Code Forensics / AppSec QA (конвейер Supervisor → Architect → Coder → Auditor)
**Состояние:** `master` 20062e8 + незакоммиченные правки (`Makefile`, `src/*`, `tools/*`, `tests/*`, `.gitignore`, новые `doc/roadmap.md`, `tests/test_machine.c`, `tests/test_network_unit.c`, `tests/test_loader.c`, `tests/test_compile.c`)
**Инструменты:** `cc (gcc 12.3) -Wall -Wextra -pedantic -std=c11 -O2`, `-Werror`, `-fanalyzer`, `-fsanitize=address,undefined` (в т.ч. `-O0`), Valgrind 3.26, `make check`

**Вердикт: [APPROVED]**

Все 6 пунктов чек-листа выполнены. Критов нет; ниже — только замечания уровня Opt, не блокирующие приёмку.

---

## 1. Память и ресурсы — OK

- Valgrind (`printf 'AB' | ./vm1 sorter.txt`): `1,775 allocs / 1,775 frees`, `in use at exit: 0`, `ERROR SUMMARY: 0 errors`, RC=0.
- ASan+UBSan: все 4 unit-сьюта (`-O0`, линковка успешна) + `vm1` в режимах plain-stdin и `LDR1` — без репортов, RC=0.
- `-fanalyzer`: единственное предупреждение — `leak of '<unknown>'` в `network_push_pair` по пути `main → allocate_machine → E() → exit(1)`. Ложноположительное: это fail-fast выход из процесса (ОС回收), Valgrind утечек не подтверждает.
- `fclose`/`free` на штатных путях корректны: `sorter_to_network` → `fclose` в `allocate_machine`, `network_free` + `free(vm1)` в `main`, все `net_free`/`seq_free`/`free(ready)` в `network.c`, `fclose(fin/fout)` в `compile.c`.

## 2. Безопасность — OK

- Переполнения буфера закрыты: `read_word`/`write_word` проверяют `word < SPACE_WORDS`; `sorter_to_network` валидирует `a,b < SPACE_SIZE`, `a != b`, `a < b`; `maybe_load_program_from_stdin` проверяет `base < cap` и `words <= cap - base` (арифметика без wraparound: `cap - base` вычисляется после проверки `base < cap`); `seed_interval_pair` ограничен `program_word_limit() ≤ min(512, SPACE_WORDS)`, `base_word+3` внутри лимита.
- UB исключены: `ceil_log2_u32` гвардит `n <= 1` (нет `__builtin_clz(0)`); `set_bit_u8`/`GETBIT` вызываются только с `off = i % CHAR_BIT` (0–7); цикл `for (idx i = 0; i < words; i++)` ограничен `words ≤ 4095` — без wraparound `uint16_t`.
- Опасных функций нет (`strcpy`/`sprintf`/`gets` отсутствуют); ввод парсера ограничен: `fgets` 256/4096 + строгий `sscanf("%u %u %c")` с проверкой `> 0xFFFF`; overlong-строки fail-safe (чанки не матчат пару → пропуск).
- Инъекции: `sorter.txt` — доверенный артефакт сборки; мусорные строки пропускаются, нарушения инвариантов (`a==b`, `a>b`, вне диапазона) — fail-closed через `E()` (RC=1). Проверено функционально: plain-stdin `Hi` → 2 байта выхода без потерь (pushback работает), `LDR1+AB` → `00 41` (эхо), усечённый `LDR1` → RC=1 с протоколной ошибкой.
- Состояния гонки: проект однопоточен; `wire_used`/`stdin_pushback` — глобалы, но используются строго последовательно. Отмечено как Opt (см. таблицу).

## 3. Сложность — OK

- `run_network`: O(Σ layers × pairs) ≈ 4.0M пар × 2 фазы за цикл; цикл ограничен `LIFE=16`. Скрытых циклов нет; `wire_used` сбрасывается `memset(65 520)` только при смене слоя — пренебрежимо.
- Фактическая сеть: `sorter.txt` = 3 997 174 строк, 135 пустых разделителей → 136 слоёв = `LAYER_N = 16·17/2` — совпадает с roadmap.
- Лишних аллокаций в горячем цикле нет: сеть строится один раз в `allocate_machine`; `layer_reserve`/`network_reserve` — амортизированно O(1) через удвоение.

## 4. Читаемость — OK

- Именование описательное (`is_wire_reused`, `wire_used_reset`, `seed_intervals_from_program`, `swap_sort_and_partner`); стиль соответствует `.clang-format` (LLVM); избыточности нет (`STATIC inline` устранён, остался один `STATIC` без `inline` — линковка `-O0` подтверждена).
- `stage1_from_left/right` сохраняют `partner` в локальную переменную до `move/copy` — порядок безопасен.
- Макрос `STATIC` (`static` ↔ пусто под `UNIT_TEST`) — осознанный приём для unit-тестов internals, контракт не нарушает.

## 5. Тесты — OK (каждая функция покрыта)

- `src/machine.c` (~44 функции + `main`): `tests/test_machine.c` — 30 `test_*`, **149 CHECK, fail=0** (включая ASan `-O0`). Покрыты все функции, включая `network_last_layer`, `buffered_getc`, `channel_read/write` (mock stdin/stdout), полный путь `maybe_load_program_from_stdin` (LDR1 + pushback + truncated), `parse_args_or_die`/`allocate_machine`/OOB-пути через fork, `seed_intervals_from_program`, `stage1_init`, `run_cycle`, `run_channels` (LDR1 echo end-to-end).
- `src/network.c` (13 функций + `main`): `tests/test_network_unit.c` — **43 CHECK, fail=0** (`parse_u32`, `is_pow2`/`next_pow2`, `seq_add`/`seq_free`, `net_reserve`/`net_ensure_layer`/`net_add`/`net_free`, `net_print`, `gen_pairwise_pow2` на всех 24 перестановках n=4 + инвариант `a<b`, `seq_pack`, `die` через fork).
- `tools/loader.c`: `tests/test_loader.c` — **36 CHECK** (`parse_u16`, `parse_u16_strict`, `trim`, `parse_vm1_file` включая пустой файл/3–5 полей/OOB, `write_ldr1` включая magic/count/base/LE-payload/tail/пустую программу, `die`/`die2`).
- `tools/compile.c`: `tests/test_compile.c` — **24 CHECK** (`parse_long`, `encode_vm1` границы/переполнения/null, `trim`, CLI round-trip, `die`/`die2`).
- Интеграция: `test_network` (256-проекция, pass) + `test_channel` (echo/chain/dual, 12/12). Все 6 сьютов gating в `make check` (без `|| echo`), общий RC=0.
- DoD «без Wall/Wextra/Werror»: все 10 единиц трансляции собираются с `-Werror` чисто (проверено явно).

## 6. Roadmap — OK (реализация соответствует `doc/roadmap.md` v2.0)

- Этапы 0–4 DONE — достоверно: счётчики CHECK (149/43/36/24), сеть (3 997 174 пары, 136 слоёв), CLI-контракты и таблица модулей совпадают с кодом; `make check` gating из 6 сьютов.
- Этап 2: pipe-loss исправлен через pushback (проверено функционально), `ceil_log2` корректен (`ceil_log2(65520)=16`), `read_pair` строгий, `SETBIT` → `set_bit_u8`.
- Этап 5 PARTIAL: `examples/sort256_runtime.vm1` — заглушка `0 0 0 0`, что явно задекларировано в roadmap как «следующий спринт, не блокирует Demo 2» — расхождением не считается.
- Этап 6 FUTURE: вне скоупа аудита.
- DoD выполнены все 5 пунктов: `-Werror` чист, Valgrind 0 leaks, `ceil_log2`/pipe-loss/`read_pair` в порядке, покрытие пофункционое, `make check` без `|| echo`.

---

## Таблица замечаний (все — Opt, не блокируют)

| Блок кода | Тип (Крит/Warning/Opt) | Описание | Как исправить |
|---|---|---|---|
| `tools/loader.c:150-155` `parse_vm1_file` empty-sentinel | Opt | Пустой `.vm1` возвращает указатель на `static` буфер при контракте «caller frees»; корректность держится на проверке `if (nwords != 0) free()` в `main` и `(void)w2` в тесте — хрупко при переиспользовании | Вернуть контракт единообразием: аллоцировать `malloc(1)` под пустой случай либо вынести `parse_vm1_file_ex` с явным флагом владения; задокументировать в комментарии |
| `src/machine.c:61,76`, `src/network.c:78,104,132`, `tools/loader.c:135` `realloc`-паттерн | Opt | Прямое присваивание `p = realloc(...)` теряет старый блок при OOM; сейчас безопасно лишь потому, что дальше безусловный `exit`/`die` | Канонично: `tmp = realloc(...); if (!tmp) die; p = tmp` |
| `E()`/`die()`-пути (`allocate_machine`, `compile.c`, `loader.c`) | Opt | Fail-fast через `exit()` пропускает `fclose`/`free` (дескриптор sorter, `fin`); ОС回收, Valgrind чист — стиль принят, но для библиотечного переиспользования не годится | Либо оставить как есть (документировать fail-fast), либо вернуть коды ошибок вверх по стеку |
| Глобалы `wire_used[65520]`, `stdin_pushback[8]` (`src/machine.c`) | Opt | Скрытое состояние, не потокобезопасно; использование строго последовательно, требования многопоточности в контракте нет | При рефакторинге — вынести в `struct machine_ctx` / передавать параметром; сейчас не трогать |
| `__builtin_clz` без MSVC-ветви (`src/machine.h`) | Opt | Сборка только GCC/Clang; проект де-факто Linux-only (`fork`, `fmemopen` в тестах), контракт это допускает | При нужде портируемости — добавить `#ifdef _MSC_VER` с `_BitScanReverse` |
| `examples/sort256_runtime.vm1` — заглушка | Opt | Соответствует roadmap (Этап 5 PARTIAL), функциональных претензий нет | Реализовать в следующем спринте по контракту из шапки файла |
| Незакоммиченные правки (весь diff + 4 новых файла) | Opt | Аудит проведён по рабочему дереву, а не по коммиту | Закоммитить ревизию (`fix/audit-*`); перепроверка после исправлений — по хэшу |

---

*Проверено явно: `make clean && make all` RC=0; `make check` RC=0 (149+43+36+24, `TOTAL: all cases passed`, `sort: pass=12 fail=0`); `-Werror` по всем 10 TU RC=0; ASan+UBSan `-O0` по 4 сьютам RC=0; Valgrind RC=0, 0 errors/0 leaks; `-fanalyzer` — только указанный FP. Готов к мерджу.*
