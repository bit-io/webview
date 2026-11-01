# API

## Typy
* `Webview` — uchwyt okna (`h == 0` gdy tworzenie sie nie udalo; sprawdz `is_valid`).
* `Reply { ok, json }` — wynik funkcji z `bind_fn`. `ok=false` odrzuca obietnice JS.
* `Event { kind, name, id, req }` — z `next`: `kind` 1 = wywolanie z JS, 0 = timeout, -1 = okno zamkniete.

## Okno
`open(debug) -> Webview`, `is_valid(w)`, `raw_handle(w)`, `set_window_title(w, t)`,
`set_window_size(w, szer, wys, hint)`, `hint_none/min/max/fixed()`, `load_url(w, url)`, `load_html(w, html)`,
`init_js(w, js)` (JS przed kazda strona), `eval_js(w, js)`, `stop(w)`, `close(w)`.
Wszystkie zwracaja kod `webview_error_t` (0 = OK).

## JS -> H#
* `bind_fn(w, "nazwa", |req: string| -> Reply is ... end)` — `window.nazwa(...)` w JS zwraca Promise.
* `bind_raw(w, "nazwa")` + `next` / `respond` — wlasna petla.
* `unbind_fn(w, "nazwa")`.
* Argumenty: `arg_count`, `arg_str`, `arg_int`, `arg_bool`, `arg_raw` (surowy JSON), `quote` (tekst -> literal JSON).
* Odpowiedzi: `reply_str`, `reply_int`, `reply_bool`, `reply_null`, `reply_json`, `reply_err`.

## Petla
* `serve(w)` — UI w biezacym watku + obsluga `bind_fn` w watku roboczym; wraca po zamknieciu okna.
* `run_ui(w)` — tylko UI; odpowiadasz sam (`next`, `respond`, `respond_reply`).

## Surowe bindingi
`webview_sys`: `webview_create, _destroy, _run, _terminate, _dispatch, _get_window, _get_native_handle,
_set_title, _set_size, _navigate, _set_html, _init, _eval, _bind, _unbind, _return, _version`
(typy: wskaznik = `int`, C `int` = `i32`, `const char*` = `string`).
Parametry callbackow sa `int`, bo H# nie tworzy wskaznikow na funkcje; do callbackow uzywaj `bind_fn`.

## Bezpieczenstwo
Wstawiajac dane do `eval_js` zawsze przepusc je przez `quote`.
