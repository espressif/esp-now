from esp_docs.conf_docs import *  # noqa: F403,F401

languages = ['en']
idf_targets = ['esp32', 'esp32s3', 'esp32c3', 'esp32c2', 'esp32c6']

extensions += ['sphinx_copybutton',
               # Needed as a trigger for running doxygen
               'esp_docs.esp_extensions.dummy_build_system',
               'esp_docs.esp_extensions.run_doxygen',
               ]

# link roles config
github_repo = 'espressif/esp-now'

# context used by sphinx_idf_theme
html_context['github_user'] = 'espressif'
html_context['github_repo'] = 'esp-now'

html_static_path = ['../_static']

# Extra options required by sphinx_idf_theme
project_slug = 'esp-now'

# Contains info used for constructing target and version selector
# Can also be hosted externally, see esp-idf for example
versions_url = '_static/esp-now_versions.js'
