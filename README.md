# Regex matcher

Проект собирает исполняемый файл `regex_matcher`, который повторяет интерфейс из `run.sh`:

```bash
regex_matcher <path_to_re> <path_to_text> <path_to_out>
```

Для подготовки архива для проверяющей системы:

```bash
./package.sh
```

После этого в корне появится `test_bins.tar.gz`; при распаковке в `/opt/test_bins` внутри будет `/opt/test_bins/regex_matcher`.
