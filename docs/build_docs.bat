@echo off
doxygen docs/Doxyfile
sphinx-build -b html docs docs/_build/html