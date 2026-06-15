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

Результат сравнения:

== C++ regex_matcher ==
real 3,12
user 31,70
sys 0,14
== Python re.findall reference ==
real 52,16
user 51,38
sys 0,65

