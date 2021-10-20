# aquabsd-pkg-repo
Root of the aquaBSD PKG repository, and code necessary to generate it.
The general idea is very similar to the ports system and `poudriere(8)` on FreeBSD, except this is not that.
Individual packages can be built by running the `build.sh` script in each directory, and the resulting package can be installed as so:

```sh
# pkg add package.pkg
```
