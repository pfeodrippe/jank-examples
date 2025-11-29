``` shell
PATH="/Users/pfeodrippe/dev/jank/compiler+runtime/build:$PATH" jank --module-path src run-main eita
PATH="/Users/pfeodrippe/dev/jank/compiler+runtime/build:$PATH" jank -Ivendor/flecs/distr -lvendor/flecs/distr/libflecs.dylib --module-path src run-main my-example start-server
PATH="/Users/pfeodrippe/dev/jank/compiler+runtime/build:$PATH" llbd jank -I./vendor/flecs/distr --module-path src run-main my-example start-server
```

``` shell
export PATH="/Users/pfeodrippe/dev/jank/compiler+runtime/build:$PATH"
jank -I./vendor/flecs/distr \
      -l/Users/pfeodrippe/dev/something/vendor/flecs/distr/libflecs.dylib \
      --module-path src \
      run-main my-flecs -main
```
