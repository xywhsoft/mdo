# Plugin directory metadata presentation

Browser Use 0.1.2 is a current directory ZIP. Its installed `plugin.get` result
contains the name, description and version at the top level, from extension.json.
The plugin workbench incorrectly read those fields from the nested execution
recipe and substituted "旧格式插件" when the optional description was missing.
That fallback was not a format validation result.

The list, detail view, search, credits, update identity and confirmation names now
use directory metadata. Entry points and overrides still use the execution recipe.
Empty descriptions use the shared neutral fallback in all three languages. The
obsolete translation is removed. Locale subscription is owned by the workbench
and disposed with it, rather than added on every rendered plugin row.

Validation: 33 targeted frontend checks pass, including current metadata with a
recipe containing no display fields, root metadata search, details, optional empty
descriptions, directory creation/removal, prompt choices, update behavior,
plugin loading/cleanup, pagination and language-pack coverage.
The real installed Browser Use reports version 0.1.2 and a nonempty description;
no account, plugin installation or running-client files were modified.

The ecosystem contract now explicitly maintains one current archive scheme.
This is a client source fix; it does not replace the user's packed executable or
publish a new client release.
