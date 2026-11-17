"""Synthetic recipe identities only; no model weights or runtime are executed."""
def recipe():
    labels = {'zh-CN': '测试部署', 'en-US': 'Test deployment', 'ru-RU': 'Тестовый профиль'}
    return {
        'schema': 'mdo.local-model.v1', 'default_locale': 'zh-CN',
        'i18n': {key: {'name': value, 'description': value} for key, value in labels.items()},
        'license': {'id': 'MIT', 'url': 'https://example.test/license'},
        'upstream': {'url': 'https://huggingface.co/qa/model', 'revision': 'a' * 40},
        'tags': [{'id': 'coding', 'label': labels}],
        'weights': [{'id': 'q4', 'format': 'gguf', 'quantization': 'Q4_K_M', 'files': [{
            'id': 'weights', 'path': 'model.gguf', 'role': 'weights', 'bytes': 5 * 1024**3,
            'sha256': 'b' * 64,
            'urls': ['https://huggingface.co/qa/model/resolve/' + 'a' * 40 + '/model.gguf'],
            'redirect_hosts': ['cdn.example.test']}]}],
        'runtimes': [{'id': 'cpu', 'ref': 'external-tool:qa.local-runtime',
            'version_id': '1' * 32, 'sha256': 'c' * 64, 'engine': 'llama.cpp',
            'build': 'test-build', 'platform': 'windows-x86_64', 'backend': 'cpu',
            'min_os': 'Windows 10 x64', 'cpu_features': ['avx2']}],
        'profiles': [{'id': 'default', 'label': labels, 'weight': 'q4', 'runtime': 'cpu',
            'context_tokens': 8192, 'max_output_tokens': 4096, 'output_tokens': 2048,
            'parallel': 1, 'gpu_layers': 0, 'capabilities': ['text', 'tools'],
            'reasoning': {'kind': 'provider-default'},
            'requirements': {'ram_bytes': 8 * 1024**3, 'vram_bytes': 0,
                'disk_bytes': 6 * 1024**3, 'margin_bytes': 1024**3, 'basis': 'estimate',
                'source_url': 'https://example.test/requirements', 'checked_at': '2026-11-08'}}],
    }


def manifest():
    return {'kind': 'local-models', 'id': 'local-model:qa.test-model', 'name': 'Test model',
        'alias': 'test-model', 'entry': 'model.json', 'platforms': ['windows-x86_64'],
        'version': '1.0.0', 'description': 'Synthetic test; not a deployable model'}
