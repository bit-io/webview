# webview — bindingi H# do webview/webview

Okno z silnikiem przegladarki (WebKitGTK na Linuksie) dla H#. Calosc oparta na `extern static`
(bit.io wymaga linkowania statycznego deklaracji FFI) i podzielona na trzy warstwy:

| modul | co to |
|---|---|
| `webview_sys`  | 1:1 C API libwebview >= 0.12.0 (17 funkcji), tylko w watku UI |
| `webview_shim` | `libhwebview.a` (`native/hwebview.c`): kolejka zdarzen i posty do watku UI |
| `webview`      | wysokopoziomowe API: `bind_fn` z domknieciami H#, `serve`, `eval_js`, helpery JSON |

**Dlaczego jest cienka nakladka w C?** H# nie umie przekazac do C wskaznika na funkcje, a `webview_bind`
i `webview_dispatch` go wymagaja. `hwebview.c` (~270 linii, bez zaleznosci poza libwebview) trzyma te
callbacki, a H# odpytuje kolejke zwyklymi wywolaniami `extern static`.

## Szybki start

```
! Bit.hk aplikacji
[build]
-> flags => --dynamic     ! GTK/WebKit nie zlinkuja sie do statycznego binarki musl

[dependencies]
-> webview => *           ! albo: path ../sciezka/do/webview
```

```
use "bit -> webview" from "webview"

fn main() is
    let w: webview::Webview = webview::open(true)
    webview::set_window_title(w, "Hello")
    webview::bind_fn(w, "greet", |req: string| -> webview::Reply is
        return webview::reply_str("Hello, " + webview::arg_str(req, 0) + "!")
    end)
    webview::load_html(w, "<button onclick='greet(\"H#\").then(alert)'>go</button>")
    webview::serve(w)     ! blokuje do zamkniecia okna
    webview::close(w)
end
```

Pelne przyklady: `examples/hello`, `examples/counter` (reczna petla zdarzen i `eval_js`).

## Wymagania i budowanie

* Linux, libwebview **>= 0.12.0** (API z kodami bledow), `cc` + `ar`.
* `bit build` sam buduje `libhwebview.a` (`[build] native` w `Bit.hk`).
* libwebview musi byc zainstalowana. Jesli nie masz jej w systemie:
  `HWV_BUILD_WEBVIEW=1 sh native/build.sh` zbuduje wspoldzielone `libwebview.so` z upstream
  (potrzebne: git, cmake, g++, `libgtk-3-dev`, `libwebkit2gtk-4.1-dev`; uruchamiaj z `LD_LIBRARY_PATH=native`).
* Uzywasz **statycznej** `libwebview.a`? Dodaj w swoim programie flagi GTK/WebKit, np.
  `extern static [c, "webkit2gtk-4.1"] is end` i `extern static [c, "gtk+-3.0"] is end`
  (upstream nie dostarcza pliku pkg-config, a statyczna biblioteka nie niesie zaleznosci).

## Watki

Watek wolajacy `serve()` / `run_ui()` jest watkiem UI. Domkniecia z `bind_fn` dzialaja w watku roboczym.
Wszystkie wywolania UI z `webview` (`set_window_title`, `load_html`, `eval_js`, `bind_fn`, ...) sa
kolejkowane do watku UI i bezpieczne z kazdego watku, takze przed `serve()`. Surowe funkcje z
`webview_sys` wolno wolac tylko w watku UI.

## Testy

`sh tests/run.sh /sciezka/do/hsharp` kompiluje shim, atrape libwebview (bez GTK i okna) i sprawdza:
helpery JSON, `bind`/odpowiedzi/odrzucenia, duplikaty, zamkniecie oraz oba przyklady.
Test **nie** otwiera prawdziwego okna.

## Ograniczenia v0.1

* Tylko Linux (macOS/Windows: brak flag linkera dla frameworkow/WebView2).
* Funkcje `post_*` zwracaja blad kolejkowania, nie wynik samej operacji (np. duplikat `bind` wykrywa `bind_fn`).
* Nazwy `webview_*` sa zajete przez FFI, wiec API wysokiego poziomu ma inne nazwy
  (`open`, `serve`, `close`, `stop`, `load_html`, `load_url`, `set_window_*`, `bind_fn`).
* Liczby zmiennoprzecinkowe: uzyj `arg_raw` i wlasnego parsowania.
