# Comparação com o código nightly Qt6 enviado

Arquivo analisado: `qelectrotech-source-mirror-nightly-qt6.zip`, versão declarada no CMake `0.200.1`, SHA-256 `d09f49ec2a6b96b0456bfd2a63896a23ad1979bf50c666d47de41dd5511714cd`.

O anexo já inclui `BomExport`, o diálogo de exportação CSV e os campos de dados comerciais descritos em `docs/smart-device-bom.md`. Portanto, gerar uma lista de materiais em CSV não é uma capacidade exclusiva desta versão industrial. Os módulos em `sources/automation`, incluindo a documentação industrial e a legenda gráfica automática, não estão nesse ZIP.

| Operação | Código anexado | Versão industrial desta alteração |
| --- | --- | --- |
| Exportar lista de materiais CSV com campos comerciais | Já disponível | Preservado; tabelas industriais também podem ser exportadas |
| Gerar materiais em folhas nativas editáveis com paginação | Sem o gerador industrial | Comando próprio no menu Industrial |
| Atualizar essas folhas sem acumular cópias | Sem o gerador industrial | Reexecução substitui somente a seção escolhida |
| Gerar legenda com os desenhos reais dos símbolos usados | Sem o módulo de legenda industrial | Comando próprio, com definições repetidas reunidas |
| Recuperar as folhas anteriores após atualização | Sem esse fluxo industrial | Um Ctrl+Z restaura o lote anterior, inclusive suas edições |
| Rastrear conexões e gerar relatórios industriais | Sem os módulos desta implementação | Disponível no menu Industrial |

Isso demonstra mais automação documental nesta versão, mas não estabelece superioridade geral de estabilidade, desempenho ou todos os recursos. Há outras diferenças entre os códigos, inclusive arquivos novos de posicionamento de textos no anexo, que não foram importadas indiscriminadamente nem validadas como parte desta alteração.

O executável `qelectrotech-0.200.1-qt6+git11256-x86-win64-readytouse.zip` excede o limite de transferência de 32 MiB da ferramenta de leitura de anexos deste ambiente. Seu conteúdo binário não foi inspecionado nem executado nesta comparação. O nome do arquivo, isoladamente, não comprova quais recursos ele contém.

## Uso das novas funções

1. Abra o `.qet` e cadastre os dados conhecidos dos componentes.
2. Execute **Industrial → Gerar/atualizar lista de materiais** ou **Industrial → Gerar/atualizar simbologia**.
3. Revise as pendências comerciais e exporte as folhas desejadas em PDF, ou use **Exportar tabelas CSV**.
4. Depois de mudar o esquema/dados, execute novamente o comando. Ele lê o projeto atual e substitui suas próprias folhas. Não há sincronização contínua ao salvar.

A geração não inventa acessórios ausentes do esquema, referências comerciais ou metragem física. As regras de agrupamento, quantidade, contatos auxiliares e exclusão estão descritas no [guia industrial](README.md).
