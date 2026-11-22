# Watchdog — Ortak Sözleşme (CONTRACT)

Bu belge, üç modülün birbirine nasıl bağlandığını tanımlar. Her öğrenci geliştirmesini bu sözleşmeye göre yapar. Değişiklik gerektiğinde önce ekipçe karar verilir, ardından bu dosya güncellenir.

**Kaynak:** Sakarya Üniversitesi Yazılım Mühendisliği, 2026–2027 System Programming Term Project 1.

## 1. Dosya sahipliği (proje PDF'i, Bölüm 4)

| Sorumlu | Dosyalar | Görev |
| --- | --- | --- |
| Student 1 | `src/main.c`, `include/signal_manager.h`, `src/signal_manager.c` | `sigaction()` ile handler kurulumu; SIGINT/SIGTERM/SIGUSR1; SIGCHLD/SIGALRM bayrakları; ana döngü ve graceful shutdown |
| Student 2 | `include/process_manager.h`, `src/process_manager.c`, `workers/worker.c` | `fork()`/`exec()`, `SIGCHLD` sonrası `waitpid()`, restart limiti, periyodik kontrol ve `setitimer()` |
| Student 3 | `include/logger.h`, `src/logger.c`, `Makefile`, `README.md`, `tests/test_scenarios.sh` | Loglama, SIGUSR1 raporu, build, test ve dokümantasyon |
| Hep birlikte | `docs/REPORT.pdf`, `docs/CONTRACT.md` | Rapor ve ortak sözleşme |

Her öğrenci kendi modülündeki değişiklikleri kendi GitHub hesabıyla commit eder. Entegrasyon, kod inceleme ve son testler ortak sorumluluktur.

## 2. Ortak ayarlar

| Ayar | Değer | Tanımlandığı yer |
| --- | --- | --- |
| Worker sayısı | `2` | `process_manager.h`: `WORKER_COUNT` |
| Worker başına maksimum restart | `3` | `process_manager.h`: `MAX_RESTARTS` |
| Worker executable | `workers/worker` | `process_manager.h`: `WORKER_PATH` |
| Worker mod değişkeni | `WATCHDOG_WORKER_MODES` | `process_manager.h`: `WORKER_MODES_ENV` |
| Timer interval | `1` saniye | `signal_manager.h`: `TIMER_INTERVAL_SEC` |
| Shutdown timeout | `5` saniye | `signal_manager.h`: `SHUTDOWN_TIMEOUT_SEC` |
| Log dosyası | `watchdog.log` | `logger.h`: `LOG_FILE` |
| Ana executable | `watchdog` | `Makefile` |

Program, worker yolu göreli olduğu için **repo kök dizininden** `./watchdog` komutuyla çalıştırılır.

## 3. Genel kurallar

- Başarı/hata belirten fonksiyonlar başarıda `0`, hatada `-1` döndürür. Sayı döndüren fonksiyonlar `0` veya daha büyük bir değer, hatada `-1` döndürür. `void` fonksiyonlar istisnadır.
- Sistem çağrılarının dönüş değerleri kontrol edilir. Hatalar, logger kullanıma hazırsa `ERROR` olayıyla kaydedilir; logger başlatılamadıysa `stderr` üzerinden bildirilir.
- Sinyal handler'ları yalnızca `volatile sig_atomic_t` bayrakları ayarlar. Handler içinde `logger_*`, `printf`, `malloc`, `fork`, dosya işlemleri veya süreç yeniden başlatma yapılmaz.
- **Zombie process bırakılmaz:** SIGTERM ve SIGKILL sonrası dahil sonlanan bütün child'lar `waitpid()` ile toplanır.
- Başlangıçta bir hata olursa önceden başlatılan worker'lar kontrollü kapanış akışıyla sonlandırılır ve toplanır.
- Süreç tablosunun tek sahibi `process_manager.c` dosyasıdır. Diğer modüller tabloyu doğrudan değiştirmez; `pm_get_status()` ile kopya alır.

## 4. Ortak veri yapıları (`process_manager.h`)

```c
#include <sys/types.h>

typedef enum {
    PM_STATE_RUNNING,                /* worker çalışıyor */
    PM_STATE_EXITED,                 /* normal exit(0) veya shutdown sırasında sonlanma */
    PM_STATE_CRASHED,                /* beklenmedik sonlanma, restart işlemi bekleniyor */
    PM_STATE_RESTART_LIMIT_REACHED,  /* restart hakkı bitti */
    PM_STATE_START_FAILED           /* yalnızca ilk başlatmada fork() başarısız */
} ProcessState;

typedef struct {
    int          worker_id;      /* 1..WORKER_COUNT */
    pid_t        pid;            /* son bilinen PID; hiç başlatılamadıysa -1 */
    ProcessState state;
    int          restart_count;  /* 0..MAX_RESTARTS */
} ProcessInfo;
```

**Başlatma ve yeniden başlatma hataları:**

- İlk başlatmada `fork()` başarısızsa ilgili worker `PM_STATE_START_FAILED` olur, `pid = -1` tutulur, `ERROR` loglanır ve `pm_start_all()` `-1` döndürür. Ana program o ana kadar başlatılmış child'lar için shutdown/cleanup yapar.
- **Restart girişiminde `fork()` başarısızsa** `ERROR` loglanır ve bu girişim bir restart hakkı tüketir (`restart_count++`). Hâlâ hak varsa worker `PM_STATE_CRASHED` durumunda kalır; bir sonraki `pm_periodic_check()` çağrısında tekrar denenir. Sayaç `MAX_RESTARTS` değerine ulaşırsa `PM_STATE_RESTART_LIMIT_REACHED` olur, `RESTART_LIMIT_REACHED` loglanır ve daha fazla deneme yapılmaz. Böylece sonsuz retry oluşmaz. `PM_STATE_START_FAILED` bu durum için kullanılmaz.
- Worker'da `exec()` başarısızsa child, `exit()` değil `_exit(127)` kullanır. Parent bunu `waitpid()` üzerinden non-zero exit olarak görüp normal restart kurallarını uygular (en fazla üç restart girişimi). Ayrı bir exec hata kanalı olmadığı için yalnızca `127` kodu, tek başına hatanın kesin olarak `exec()` kaynaklı olduğunu kanıtlamaz.
- `pm_stop_launching()` çağrısından sonra bekleyen retry'lar dâhil hiçbir yeni worker başlatılmaz.
- `PM_STATE_START_FAILED` kayıtları `pm_running_count()` sonucuna katılmaz.

`PM_STATE_CRASHED` geçici durumdur; başarılı restart sonrası durum `PM_STATE_RUNNING` olur. `pm_get_status()` normal biten ve limiti dolan worker kayıtlarını da döndürür.

## 5. Fonksiyon sözleşmeleri

### 5.1 `process_manager.h` — Student 2

| Fonksiyon | Açıklama |
| --- | --- |
| `int pm_start_all(void)` | `WORKER_COUNT` worker'ı `fork()` + `exec()` ile başlatır. Hepsi başlatıldıysa `0`, aksi hâlde `-1`; oluşturulmuş child'ların temizliğini main başlatır. |
| `void pm_reap_children(void)` | `waitpid(-1, ..., WNOHANG)` ile bekleyen tüm child'ları toplar; `0` veya `ECHILD` durumunda durur, `EINTR` durumunda tekrar dener. Çıkış nedenini belirler, loglar, izin varsa restart eder. |
| `int pm_start_timer(unsigned int seconds)` | `setitimer()` ile periyodik SIGALRM başlatır; `seconds == 0` geçersizdir. Başarı `0`, hata `-1`. |
| `void pm_stop_timer(void)` | Periyodik timer'ı durdurur. |
| `void pm_periodic_check(void)` | SIGALRM bayrağı işlendiğinde main'den çağrılır; bekleyen sonlanmaları kontrol eder ve varsa `PM_STATE_CRASHED` durumunda kalmış, hakkı olan başarısız restart denemelerini bir sonraki tick'te yeniden dener. Kendi başına timer kurmaz; shutdown başladıysa restart yapmaz. |
| `void pm_stop_launching(void)` | Bundan sonra yeni worker başlatılmasını ve restart yapılmasını engeller. |
| `void pm_terminate_all(void)` | Hâlâ çalışan worker'lara SIGTERM gönderir; beklemez. |
| `int pm_running_count(void)` | Son bilinen süreç tablosuna göre aktif child sayısını döndürür; güncel sonuç için önce `pm_reap_children()` çağrılır. |
| `void pm_kill_remaining(void)` | Hâlâ çalışan child'lara SIGKILL gönderir; beklemez. |
| `int pm_get_status(ProcessInfo *out, int capacity)` | Kayıtları `worker_id` sırasıyla `out` dizisine kopyalar; en fazla `capacity` kayıt kopyalayıp sayıyı döndürür. `capacity < 0` veya `capacity > 0 && out == NULL` için `-1`; `capacity == 0` için `0`. |

**Önemli:** Timer kurulumu Student 2'ye aittir; SIGALRM handler'ının kurulumu Student 1'e aittir.

### 5.2 `logger.h` — Student 3

| Fonksiyon | Açıklama |
| --- | --- |
| `int logger_init(const char *path)` | Log dosyasını append modunda açar; `0` / `-1`. |
| `int logger_log(const char *event, const char *fmt, ...)` | `[YYYY-MM-DD HH:MM:SS] EVENT \| DETAILS` biçiminde satır yazar. Timestamp ve ayıracı logger ekler, her satırda flush yapar; `0` / `-1`. |
| `int logger_report_status(void)` | `pm_get_status()` ile bütün worker kayıtlarını alıp her biri için `STATUS_REPORT` kaydı yazar; `0` / `-1`. |
| `void logger_close(void)` | Log dosyasını kapatır. |

### 5.3 `signal_manager.h` — Student 1

| Fonksiyon | Açıklama |
| --- | --- |
| `int sm_init(void)` | SIGINT, SIGTERM, SIGUSR1, SIGCHLD ve SIGALRM için `sigaction()` handler'ları kurar; `0` / `-1`. |
| `void sm_wait_event(void)` | Bekleyen olay yoksa sinyal gelene kadar yarış durumu olmadan bekler. |
| `int sm_shutdown_requested(void)` | SIGINT veya SIGTERM istenmişse `1` döndürür. |
| `int sm_take_child_event(void)` | SIGCHLD bayrağı varsa `1` döndürüp temizler. |
| `int sm_take_alarm_event(void)` | SIGALRM bayrağı varsa `1` döndürüp temizler. |
| `int sm_take_status_request(void)` | SIGUSR1 bayrağı varsa `1` döndürüp temizler. |

**Yarış durumu önlemi:** Student 1 bekleyen olayları uyumadan önce kontrol eder. `sm_wait_event()` salt `pause()` yapmaz. Sinyal bayrağının kontrolü ile uykuya geçiş arasındaki yarışı önlemek için `sigprocmask()` + `sigsuspend()` (veya eşdeğer atomik bekleme tasarımı) kullanılır. Sinyal maskesini değiştirme ve geri yükleme işlemleri doğru yürütülür. Bu mekanizma sayesinde bir olay tam bekleme öncesinde geldiyse ana döngü süresiz uyumaz.

## 6. Main loop ve graceful shutdown — Student 1

Aşağıdaki akış **mantıksal taslaktır**; `sm_wait_event()` gerçeklenirken olay kontrolü ve sinyal maskeleme atomik tasarlanacaktır.

```text
logger_init(LOG_FILE)
sm_init()
logger_log("WATCHDOG_STARTED", ...)
pm_start_all()                        # hata varsa cleanup/shutdown
pm_start_timer(TIMER_INTERVAL_SEC)    # hata varsa cleanup/shutdown

while shutdown is not requested:
    if SIGCHLD event: pm_reap_children()
    if SIGALRM event: pm_periodic_check()
    if SIGUSR1 event: logger_report_status()
    if shutdown requested: break
    sm_wait_event()                   # bekleyen flag yoksa atomik bekleme

logger_log("SHUTDOWN_STARTED", ...)
pm_stop_launching()                  # önce restart'ı engelle
pm_stop_timer()
pm_terminate_all()                   # SIGTERM gönder
deadline = clock_gettime(CLOCK_MONOTONIC) + SHUTDOWN_TIMEOUT_SEC
while pm_running_count() > 0 and clock_gettime(CLOCK_MONOTONIC) < deadline:
    pm_reap_children()
    nanosleep(short_interval)           # EINTR durumunu dikkate al

pm_reap_children()
if pm_running_count() > 0:
    pm_kill_remaining()              # SIGKILL
    # SIGKILL sonrası kalan child'ları tamamı toplanana kadar waitpid ile reap et
logger_log("WATCHDOG_STOPPED", ...)
logger_close()
```

**Shutdown zaman ölçümü:** Student 1, 5 saniyelik timeout'u `clock_gettime(CLOCK_MONOTONIC, ...)` ile ölçer; timer durdurulmuş olsa bile süre doğru ilerler. Kapatma döngüsünde `pm_reap_children()` ve `pm_running_count()` kontrol edilir, aralarda kısa bir `nanosleep()` ile beklenir ve her turda deadline yeniden kontrol edilir. Bekleme sınırsız `sigsuspend()` ile yapılmaz. Kapanış başlamadan önce veya kapanış sırasında gelen SIGCHLD olayları `pm_reap_children()` ile temizlenir. Gerekli SIGKILL sonrası kalan child'ların `waitpid()` ile toplanması tamamlanmadan Watchdog çıkmaz. `pm_stop_launching()` kapatma işlemlerinden **önce** çağrılır.

## 7. Restart kuralları — Student 2

- Normal sonlanma (`exit code 0`) → restart **yok**, state `PM_STATE_EXITED`.
- Non-zero exit code veya sinyalle beklenmedik sonlanma → restart hakkı varsa yeniden başlatılır.
- Worker başına en fazla **3 restart**: **1 ilk başlatma + 3 restart**. Sonraki beklenmedik ölümde `RESTART_LIMIT_REACHED` loglanır, tekrar başlatılmaz.
- `SIGCHLD` olayları birleşebilir. `pm_reap_children()` bu yüzden `waitpid(-1, ..., WNOHANG)` döngüsünü kullanır; bir sinyale bir child varsayımı yapılmaz.
- `pm_stop_launching()` çağrıldıktan sonra restart yapılmaz. Shutdown sırasında SIGTERM/SIGKILL nedeniyle biten child'lar crash olarak işlenmez; `PROCESS_EXITED` loglanır ve durum `PM_STATE_EXITED` olur.
- Tüm worker'lar sona erse de watchdog çalışır; SIGUSR1 raporu cevaplanır; çıkış Ctrl+C veya SIGTERM ile olur.
- İlk başlatmada `fork()` hatası `PM_STATE_START_FAILED` ve `pm_start_all() == -1` ile bildirilir. Restart sırasında `fork()` başarısızlığı `ERROR` loglanır, `restart_count` artırılır; kalan hak varsa sonraki periyodik kontrolde tekrar denenir, hak kalmadığında `RESTART_LIMIT_REACHED` durumuna geçilir. `exec()` hatasında child `_exit(127)` ile çıkar. Shutdown başladıktan sonra hiçbir yeniden deneme yapılmaz.

## 8. Worker programı — Student 2

Çalıştırma: `workers/worker <mode> [seconds]`. Varsayılan mod `normal`.

| Mod | Davranış |
| --- | --- |
| `normal` | SIGTERM gelene kadar çalışır; SIGTERM üzerine normal çıkış yapar. |
| `exit0` | `seconds` saniye sonra `exit(0)`; varsayılan **5 saniye**. |
| `error` | `seconds` saniye sonra `exit(1)`; varsayılan **5 saniye**. |
| `ignore_term` | SIGTERM'i yok sayar; SIGKILL yolunu test etmek için çalışır. |

- Bütün modlar SIGINT'i yok sayar; Ctrl+C worker'ları doğrudan değil, watchdog üzerinden kapatır.
- `WATCHDOG_WORKER_MODES` değişkeni virgülle ayrılmış `mod[:saniye]` listesidir. Örnek: `WATCHDOG_WORKER_MODES="normal,error:2" ./watchdog`.
- Değişken tanımsızsa veya bir worker için mod eksikse `normal` kullanılır. Student 2 bu veriyi okuyup worker'a argüman olarak geçirir.

## 9. Log standardı — Student 3

Biçim: `[YYYY-MM-DD HH:MM:SS] EVENT | DETAILS`.

`DETAILS`, gereken yerde `worker_id`, `pid`, `restart_count`, `exit_code`, `signal`, `error` anahtar-değer çiftlerini içerir. **Bir olay bir log satırıdır.**

| Event | Kaynağı |
| --- | --- |
| `WATCHDOG_STARTED`, `SHUTDOWN_STARTED`, `WATCHDOG_STOPPED` | Student 1 (`main.c`) |
| `PROCESS_STARTED`, `PROCESS_EXITED`, `PROCESS_CRASHED`, `PROCESS_RESTARTED`, `RESTART_LIMIT_REACHED` | Student 2 (`process_manager.c`) |
| `STATUS_REPORT` | Student 3 (`logger_report_status`) |
| `ERROR` | Herkes (handler'lar hariç) |

```text
[2026-11-25 10:15:03] PROCESS_STARTED | worker_id=1 pid=4321 restart_count=0
[2026-11-25 10:15:09] PROCESS_CRASHED | worker_id=1 pid=4321 signal=9 restart_count=0
[2026-11-25 10:15:09] PROCESS_RESTARTED | worker_id=1 pid=4390 restart_count=1
[2026-11-25 10:15:20] STATUS_REPORT | worker_id=1 pid=4390 state=RUNNING restart_count=1
```

`STATUS_REPORT` içindeki `state`, `PM_STATE_` önekinden sonraki isimdir: `RUNNING`, `EXITED`, `CRASHED`, `RESTART_LIMIT_REACHED` veya `START_FAILED`.

## 10. Derleme — Student 3

Önerilen başlangıç ayarları:

```makefile
CFLAGS = -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Wpedantic
```

- POSIX arayüzlerinin bildirimleri için `_POSIX_C_SOURCE=200809L` kullanılır.
- Student 1 tarafından Ubuntu 24.04 (GCC 13.3, glibc 2.39) üzerinde `setitimer()`, `clock_gettime()`, `nanosleep()`, `sigsuspend()`, `sigprocmask()`, `waitpid()` ve `kill()` için bu bayrakların yeterli olduğu bildirilmiştir; `_DEFAULT_SOURCE` eklenmeyecektir. Student 3 gerçek Makefile ile yeniden doğrular.
- `make` hem `watchdog` hem `workers/worker` çıktısını üretir. Derleyici uyarıları giderilir.
- Programın çalıştırıldığı dizin repo kökü olmalıdır.

## 11. GitHub düzeni

- `main`: ortak ve teslim edilecek sürüm.
- `feature/signal-management`: Student 1.
- `feature/process-management`: Student 2.
- `feature/logging-testing`: Student 3.
- Repo zaten mevcut bir `main` ve commit geçmişine sahiptir; yeni `main` oluşturulmaz, geçmiş sıfırlanmaz.
- Önce güncel `main` üzerinde bu sözleşme ve `.gitignore` yer alır. Herkes feature branch'ini bu noktadan açar.
- Herkes kendi sorumlu dosyalarını kendi hesabından, küçük ve açıklayıcı commit'lerle gönderir.
- PR'larda mümkün olduğunca yalnızca sorumluluk alanındaki dosyalar olur. Merge yöntemi **Create a merge commit**; **Squash** veya **Rebase** kullanılmaz.
- `main` güncellenince feature branch güncel `main` ile senkronize edilir; çalışma kaybı yaşamamak için önce yerel değişiklikler kontrol edilir.
- Son sürümün `main` içeriği ile SABİS'e yüklenen dosyalar aynı olmalıdır.

## 12. Takvim

| Tarih | İş |
| --- | --- |
| 22 Kasım | `CONTRACT.md`, `.gitignore`, feature branch'ler |
| 22–24 Kasım | Eş zamanlı geliştirme, bireysel testler |
| 24 Kasım akşamı | Hazır PR'ların `main` branch'ine birleştirilmesi |
| 25 Kasım | Entegrasyon ve 7 ortak test senaryosu |
| 26 Kasım | Hata düzeltme, `REPORT.pdf`, mümkünse SABİS'e teslim |
| 27 Kasım | Yedek gün, son kontrol |

**Ortak kabul:** Modül sahipleri birbirlerinin tüm kodunu beklemez; ortak arayüzleri esas alarak paralel geliştirir. Sözleşme değişikliği gerektiğinde ilgili tüm öğrenciler değişikliği onaylar.
