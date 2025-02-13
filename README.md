# Actor_system
Drugie duże zadanie zaliczeniowe z przedmiotu [Programowanie współbieżne](https://usosweb.mimuw.edu.pl/kontroler.php?_action=katalog2/przedmioty/pokazPrzedmiot&kod=1000-213bPW) w roku akademickim 2020/21 na wydziale MIM UW.

## Wprowadzenie

Zadanie będzie polegało na zaimplementowaniu pewnej wersji wzorca aktorów. Aktory to mechanizm przetwarzania pozwalający na wielowątkowe wykonywanie zestawu zadań w ramach jednego programu. Aktor to byt, który przyjmuje komunikaty z pewnego zestawu. Przyjęcie komunikatu danego rodzaju wiąże się z wykonaniem pewnego obliczenia imperatywnego, które może mieć efekty uboczne, np. polegające na przekształceniu jakiejś globalnej struktury danych, ale też na utworzeniu nowych aktorów i wysłaniu do jakichś istiejących aktorów komunikatów. Komunikaty wysyłane w tym systemie mają charakter asynchroniczny. W obecnym zadaniu praca aktorów jest wykonywana przez wątki robocze (ang. worker threads) z określonej puli.

W niektórych realizacjach wzorca aktorów każdego aktora obsługuje osobny wątek. Tutaj tego nie robimy, gdyż przewidujemy, że liczba aktorów może być znaczna, a to prowadziłoby do dużej straty czasu na przełączanie wątków.
## Polecenie

### Szczegółowy opis puli wątków

Pulę wątków należy zaimplementować jako sposób wewnętrznej organizacji systemu aktorów. Pula ma być tworzona wraz z utworzeniem systemu wątków w opisanej poniżej procedurze actor_system_create. Powinna ona dysponować liczbą wątków zapisaną w stałej POOL_SIZE. Wątki powinny zostać zakończone automatycznie, gdy wszystkie aktory w systemie skończą działanie.
Szczegółowy opis mechanizmu obliczeń aktorów

Przy pomocy puli wątków należy zaimplenentować asynchroniczne obliczenia aktorów jako realizację interfejsu przedstawionego w dołączonym do treści tego zadania pliku "cacti.h". Zamieszczone są tam m.in. następujące deklaracje:
```
typedef long message_type_t;

#define MSG_SPAWN (message_type_t)0x06057A6E
#define MSG_GODIE (message_type_t)0x60BEDEAD
#define MSG_HELLO (message_type_t)0x0

#define CAST_LIMIT 1048576

#define POOL_SIZE 3


typedef struct message
{
    message_type_t message_type;
    size_t nbytes;
    void *data;
} message_t;

typedef long actor_id_t;

typedef void (*const act_t)(void **stateptr, size_t nbytes, void *data);

typedef struct role
{
    size_t nprompts;
    act_t *prompts;
} role_t;

int actor_system_create(actor_id_t *actor, role_t *const role);

void actor_system_join(actor_id_t actor);

int send_message(actor_id_t actor, message_t message);

actor_id_t actor_id_self();
```

Wywołanie int ret = actor_system_create(actor, role) tworzy pierwszego aktora systemu aktorów, odpowiedzialnego za rozpoczęcie i zakończenie przetwarzania, oraz sam system aktorów obsługiwany przez pulę wątków o rozmiarze POOL_SIZE. Wynik tego wywołania jest 0, gdy wykonanie zakończy się prawidłowo, zaś jest ujemny w przeciwnym wypadku. W wyniku wywołania pod wskaźnikiem actor zapisywany jest identyfikator do pierwszego aktora systemu, który obsługuje komunikaty zgodnie z zawartością parametru role – dokładniejszy opis ról znajduje się poniżej. W ramach implementacji systemu utrzymywane są struktury danych opisujące stan wewnętrzny aktorów potrzebny do ich prawidłowego wykonywania. W szczególności z każdym aktorem związana jest kolejka komunikatów do niego skierowanych. W danym systemie może funkcjonować co najwyżej CAST_LIMIT aktorów. Można przyjąć założenie, że w danym momencie funkcjonuje jeden system aktorów. Natomiast po zakończeniu działania jednego systemu powinno być możliwe utworzenie następnego.

Wywołanie actor_system_join(someact) powoduje oczekiwanie na zakończenie działania systemu aktorów, do którego należy aktor someact. Po tym wywołaniu powinno być możliwe poprawne stworzenie nowego pierwszego aktora za pomocą actor_system_create. W szczególności taka sekwencja nie powinna prowadzić do wycieku pamięci.

Aktorom w systemie aktorów można wysyłać komunikaty. Do tego służy wywołanie int ```res = send_message(someactor, msg)```, które wysyła komunikat opisany przez msg do aktora o identyfikatorze someactor. Wynik tego wywołania jest 0, jeśli operacja zakończy się poprawnie, -1, jeśli aktor, do którego wysyłamy komunikat, nie przyjmuje komunikatów (zob. opis MSG_GODIE poniżej), -2 jeśli aktora o podanym identyfikatorze nie ma w systemie. Wysłanie komunikatu wiąże się z włożeniem go do związanej ze wskazanym aktorem kolejki komunikatów. Kolejki mają stałe górne ograniczenie na swoją długość (nie mniejszą niż 1024 pozycje). Aktor działa, wyciągając komunikaty ze swojej kolejki i obsługując je zgodnie z przypisaną sobie rolą. Obsługa wyciągania komunikatów z kolejki powinna być tak zorganizowana, aby nie powodować zagłodzenia aktorów. Rola jest opisana przez zestaw wywołań umieszczonych w tablicy rozdzielczej, do której wskaźnik znajduje się w polu prompts struktury role. W tablicy rozdzielczej znajdują się wskaźniki do funkcji obsługujących komunikaty różnych typów. Typy komunikatów są tożsame z indeksami w tablicy rozdzielczej. Liczba pozycji w tablicy rozdzielczej jest zapisana w polu nprompts struktury role. Funkcje obsługujące komunikaty są typu

```typedef void (*const act_t)(void **stateptr, size_t nbytes, void *data);```

Pierwszy argument zawiera wskaźnik do stanu wewnętrznego aktora, który obsługuje komunikat. Stan ten jest zależny od implementacji konkretnego obliczenia - aktory w różnych systemach lub o różnych rolach mogą korzystać z różnych formatów tego stanu. Drugi argument to rozmiar w bajtach danych wskazywanych przez trzeci argument. Wreszcie trzeci argument to wskaźnik do fragmentu globalnego stanu, który może być odczytywany i modyfikowany przez funkcję obsługi.

Komunikaty mają format opisany w strukturze message. Pole message_type identyfikuje typ komunikatu, pole nbytes określa w bajtach, jakiej długości jest trzecie pole, wreszcie trzecie pole data to dane, które mają format rozumiany przez funkcję obsługującą komunikaty typu wskazywanego w pierwszym argumencie.

W treści takich procedur można używać funkcji ```actor_id_self()``` do określania identyfikatora aktora, do którego wysłany jest dany komunikat.

Każdy aktor obsługuje trzy predefiniowane typy komunikatów

    MSG_SPAWN
    MSG_GODIE
    MSG_HELLO

Wartości typów dwóch pierwszych są tak duże, że nie będą się pojawiać w implementacjach, a ich obsługa jest predefiniowana i nie można jej zmienić. Trzeci komunikat MSG_HELLO nie ma predefiniowanej obsługi. Jednak każdy aktor powinien obsługiwać komunikaty tego typu przez znajdującą się pod indeksem 0 funkcję w tablicy rozdzielczej roli aktora.
Obsługa MSG_SPAWN

Obsługa tego komunikatu używa pola data komunikatu jako struktury typu role. Tworzy ona nowego aktora z tak przekazaną rolą, a następnie do nowo utworzonego aktora wysyła komunikat MSG_HELLO z polem danych zawierającym uchwyt do aktora ```((void*)actor_id, gdzie actor_id jest typu actor_id_t)```, w którym następuje stworzenie nowego aktora.
#### Obsługa ```MSG_GODIE```

Obsługa tego komunikatu nie zagląda do żadnego pola komunikatu, ale powoduje przejście aktora, do którego wysłany jest ten komunikat, do stanu martwego, czyli stanu, w którym nie przyjmuje on już żadnych komunikatów. Jednak aktor w tym stanie wciąż obsługuje komunikaty, które zostały do niego wysłane wcześniej. Zwykle wysłanie komunikatu MSG_GODIE musi być poprzedzone wysłaniem komunikatu przeprowadzającego zwalnianie zasobów, którymi gospodaruje aktor (zaalokowana pamięć, zajęte deskryptory plików itp.). Odpowiedzialnością implementujących obsługę komunikatów jest zapewnienie poprawnego działania obsługi komunikatów, które docierają do aktora po komunikacie MSG_GODIE.
#### Obsługa ```MSG_HELLO```

Obsługa tego komunikatu nie jest predefiniowana. Jednak obsługa tych komunikatów jest o tyle ważna, że pozwala ona nowemu aktorowi zdobyć identyfikator pewnego aktora w systemie, aby móc do niego wysyłać komunikaty. Tym początkowo znanym nowemu aktorowi aktorem jest aktor, który go stworzył. Identyfikator tego aktora znajduje się w argumencie data funkcji obsługującej takie komunikaty. Obsługa komunikatu MSG_HELLO w argumencie stateptr otrzymuje wskaźnik do miejsca w pamięci zawierającego wartość NULL. Może ona tę wartość zastąpić wskaźnikiem do zainicjalizowanego przez siebie stanu wewnętrznego aktora.

Samo prawidłowe działanie systemu może wymagać, aby w roli aktora tworzącego była obsługa komunikatów pozwalajacych na dowiedzenie się o wielu innych aktorach, jacy znajdują się w systemie. Jednak konkretne rozwiązania muszą być zależne od rodzaju wykonywanych przez system obliczeń.
#### Obsługa sygnałów

Programy, w których aktywnie działa jakaś pula wątków, powinny mieć automatycznie ustawioną obsługę sygnałów. Ta obsługa powinna zapewniać, że program po otrzymaniu sygnału (SIGINT) zablokuje możliwość dodawania nowych aktorów do działających systemów aktorów oraz przyjmowania komunikatów, dokończy obsługę wszystkich komunikatów zleconych dotąd działającym aktorom, a następnie zniszczy działający system aktorów.

Dla ułatwienia implementacji można założyć, że zaimplementowana biblioteka będzie testowana w taki sposób, iż wątki nie będą ginęły w testach.

### Opis programu silnia

Program silnia powinien wczytywać ze standardowego wejścia pojedynczą liczbę n, a następnie obliczać za pomocą systemu aktorów liczbę n!. Każdy aktor ma otrzymywać w komunikacie dotychczas obliczoną częściową silnię k! wraz z liczbą k, tworzyć nowego aktora i wysyłać do niego (k+1)! oraz k+1. Po końcowego n! wynik powinien zostać wypisany na standardowe wyjście.
