# QElectroTech Industrial — escopo desta entrega

Esta versão usa o aplicativo QElectroTech real, com integração direta no código-fonte. O antigo patch de compilação foi substituído pelos módulos de `sources/automation`.

## Como usar no Windows

1. Extraia o ZIP inteiro para uma pasta gravável.
2. Execute `Abrir QElectroTech.bat`.
3. Abra seu projeto `.qet` ou a demonstração na pasta `documentacao`.
4. Use o menu **Industrial**.

### Bornes

**Identificar bornes na documentação** abre uma tabela com identificação documental, comentário, função, descrição textual, fabricante e referência comercial. Com bornes selecionados, edita somente eles; sem seleção, lista todos. A identificação documental pode aparecer como X4/X5 nas tabelas sem acrescentar uma legenda no símbolo. A edição não gira o borne. Ctrl+Z/Ctrl+Y desfazem/refazem o lote.

### Rastreamento e dados elétricos

**Rastrear conexões elétricas** inventaria todas as folhas, fios, terminais, potenciais e vínculos nativos entre setas. **Percorrer circuito** mostra passagens por dispositivos e distingue caminhos condicionais de continuidade metálica. Pistas geométricas não são tratadas como conexões comprovadas. Bobinas, cargas e conversores não são curtos entre potenciais.

**Editar dados elétricos das cargas** cadastra dados conhecidos no próprio `.qet`. A ferramenta individual carrega os dados quando encontra uma única carga candidata e bloqueia aplicar uma seção única a várias cargas. Para dimensionar conjuntos, use **Dimensionar alimentação, derivações, neutro e PE**: o novo motor calcula correntes por trecho e distingue alimentadores e ramais.

A coluna **Papel elétrico** permite declarar carga, fonte, proteção, contato/manobra, conversor ou borne. Corrente nominal de uma proteção, fonte ou componente não identificado não é interpretada como corrente de carga. Se o símbolo não for reconhecido, selecione-o e declare seu papel. Trocar os potenciais selecionados limpa os dados elétricos, a proteção e o curto-circuito do circuito anterior antes de carregar os dados do novo. Potenciais com marcações explícitas incompatíveis (por exemplo L1 e L2, ou N e PE) ficam pendentes; não recebem seção automática.

### Pré-dimensionamento

Selecione fios no desenho ou potenciais no rastreador. Informe os dados ausentes, inclusive comprimento físico, e confirme o perfil apresentado. O cálculo não usa o comprimento desenhado. Sem seleção, é possível apenas classificar; não se aplica uma única bitola ao quadro inteiro.

O núcleo calcula corrente/potência/tensão para CA monofásica, CA trifásica equilibrada e CC; considera fatores declarados de temperatura e agrupamento; verifica queda resistiva, Ib/In/Iz e I2 quando informados e verificação adiabática de curto quando corrente, tempo e k são suficientes. O núcleo também permite conversão de potência mecânica com rendimento; a interface de entrada de potência desta versão pede potência elétrica ativa ou corrente de placa.

A seção é gravada somente nos potenciais escolhidos. Propriedades existentes são preservadas, salvo substituição explícita. O registro do cálculo é salvo junto ao fio no `.qet`; desfazer/refazer inclui esse registro. Cor de desenho, isoladamente, não prova que um fio é PE. PE não recebe seção calculada a partir da corrente da carga.

**Perfil da ferramenta individual:** cobre/PVC 70 °C, B1, preliminar. O novo dimensionador de alimentações aceita perfis por trecho, tabelas de capacidade declaradas, diferentes materiais/métodos, neutro, PE, espectro harmônico, partida e curvas/associações de proteção. Seus critérios, condições e limites estão no [guia de dimensionamento](DIMENSIONAMENTO.md). Nenhuma das ferramentas certifica integralmente uma instalação pela NBR 5410.

### Alimentadores, derivações, neutro e PE

Abra **Industrial → Dimensionar alimentação, derivações, neutro e PE**. Cadastre a origem e seus terminais, revise as cargas rastreadas e os comprimentos físicos, agrupe fases/N/PE por cabo/circuito e confira o perfil de instalação e as proteções. A edição em lote evita repetir fatores e dados comuns em cada fio. A função **Revisar todas as origens** calcula os domínios cadastrados, incluindo CA e CC separados; **Aplicar todas verificadas** aplica o conjunto em um único lote quando todas as cargas estão cobertas e não há pendências/compartilhamentos ambíguos.

Ative **Recalcular todas as origens cadastradas ao salvar** e salve os parâmetros para automatizar os próximos salvamentos. Seções geradas pelo próprio motor podem crescer/diminuir; seções manuais ficam fixas. Se você editar manualmente uma seção automática, ela passa a ser tratada como manual. Dados insuficientes mantêm as seções anteriores, registram pendências e permitem salvar o projeto. Cálculos antigos são marcados para recálculo. Documentação técnica automática existente é regenerada quando o cálculo/estado muda; folhas manuais são preservadas.

## Documentação

**Gerar simbologia gráfica e tabelas (PDF)** exporta simbologia vetorial real, materiais, funções, bornes, condutores com origem/destino, bitolas, potenciais/circuitos, referências entre folhas, dimensionamento, propriedades ausentes e inconsistências. **Exportar tabelas CSV** produz os dados tabulares.

**Gerar/atualizar lista de materiais** percorre todas as folhas do projeto, reúne os dados comerciais, soma as quantidades e cria somente as folhas da relação de materiais. Ao executar novamente, substitui as folhas automáticas dessa lista pelos dados atuais, sem acrescentar cópias. Folhas manuais são preservadas, mesmo quando têm o mesmo título.

**Gerar/atualizar simbologia** cria somente a legenda, com desenhos vetoriais dos símbolos realmente usados, letras de identificação e descrições. Símbolos repetidos que usam a mesma definição aparecem uma vez. Atualizar a simbologia preserva a lista de materiais; atualizar os materiais preserva a simbologia.

**Gerar/atualizar documentação completa em folhas editáveis** atualiza todas as seções documentais geradas. A legenda usa gravações vetoriais do próprio símbolo em formas gráficas, sem criar componentes elétricos nem novas unidades de material. Cada geração/atualização pode ser desfeita/refeita em um único lote. Ctrl+Z também recupera edições manuais feitas nas folhas automáticas anteriores. Essas folhas são substituídas na próxima geração; mantenha conteúdo próprio em folhas manuais.

Ative **Atualizar materiais e simbologia ao salvar** para gerar/atualizar ambas as listas automaticamente no salvamento normal do projeto, sem executar os dois comandos. A opção fica gravada no `.qet`, começa desativada em projetos existentes e pode ser desfeita com Ctrl+Z. Com ela ativada, edite os dados dos componentes e salve: o programa substitui as folhas automáticas de materiais/símbolos pelos dados atuais. Folhas manuais e as demais seções são preservadas. O salvamento de um arquivo sem alterações não acrescenta novos lotes de geração.

Com a opção desativada, as listas são uma fotografia dos dados no momento da execução: depois de alterar o projeto, execute novamente o comando correspondente. Não há atualização a cada tecla nem durante a cópia de segurança em segundo plano. Use a exportação PDF do QET para imprimir/exportar as folhas desejadas. **Exportar tabelas CSV** inclui a relação de materiais com as mesmas quantidades e pendências.

Descrições extensas continuam em novas linhas/folhas com cabeçalhos repetidos, preservando o texto, inclusive caracteres Unicode, dentro das margens. As tabelas nativas antigas do projeto industrial de exemplo podem emitir avisos próprios de capacidade; isso é separado das novas folhas documentais paginadas e não foi silenciado.

Produtos só são agrupados quando fabricante, código e modelo permitem identificar equivalência. O campo nativo **model** é usado; o antigo campo documental é aceito quando ele está vazio. Códigos/modelos diferentes, inclusive em maiúsculas/minúsculas, permanecem separados. Ocorrências sem fabricante/código permanecem separadas e recebem pendências explícitas. Sem identificação, a referência da ocorrência informa página e UUID.

A quantidade nativa **quantity** é respeitada: valores numéricos são somados, com vírgula ou ponto decimal; quando o campo está vazio, conta-se uma unidade por ocorrência. Expressões com unidades ou valores não interpretáveis permanecem textuais, em linhas separadas, com pendência para revisão. O campo **exclude_from_bom** é respeitado (`true`, `1`, `yes`, `on`). Contatos escravos vinculados sem código próprio, ou com o mesmo código/fabricante do mestre, não duplicam o produto; blocos auxiliares com referência comercial própria entram separadamente. Setas de referência entre folhas não entram como unidades de material.

A lista representa os componentes cadastrados no esquema. Acessórios não desenhados/não cadastrados, trilhos, canaletas, parafusos e comprimentos físicos dos cabos não são deduzidos do desenho. Não há consulta automática a catálogos ou preenchimento de fabricante/código por adivinhação.

O projeto original continua abrindo no formato `.qet`. Formas documentais com o campo adicional `qet_symbol_vector` precisam desta versão para exibir o pictograma; versões anteriores do QET podem mostrar apenas o contorno da forma. PDF preserva a representação vetorial.

## Referências e validação

Consulta de edição em 23/10/2026: catálogo ABNT apresenta **ABNT NBR 5410:2004, Versão Corrigida:2008**; IEC apresenta **IEC 60364-5-52:2009+AMD1:2024**. Confirmar a edição não certifica os cálculos desta versão. O perfil herdado não foi convertido em uma implementação normativa completa.

- https://www.abntcatalogo.com.br/
- https://webstore.iec.ch/en/publication/103734

A compilação Windows executa testes matemáticos, integração com classes reais QET (seleção, gravação, geometria, salvar/reabrir, desfazer/refazer, editor de bornes e documentos) e a suíte CTest. O pacote passa por abertura de um `.qet` com o plugin Qt Windows e PATH sem MSYS2, além de uma janela real do editor e seu fechamento normal. O projeto completo é usado na leitura; o exemplo de uma folha `741.qet` é usado na verificação independente da janela, para não confundir avisos das tabelas antigas com inicialização do aplicativo. Verifique o resultado da execução correspondente ao commit. `validation/build-info.json` identifica o código, os projetos usados e quais verificações passaram. A verificação automatizada não equivale a aceitação manual completa da GUI no computador do usuário. Pacotes cuja verificação de execução falhe não são publicados como portáteis válidos.

A fixture industrial de integração vem do próprio QET: 50 folhas, 618 elementos e 671 fios. Seus dados são de exemplo, não do quadro do usuário. O PDF `DOCUMENTAÇÃO DO QUADRO.pdf` é referência visual; seus dados não são inseridos automaticamente na demonstração.

O pacote também inclui `documentacao/dimensionamento`: exemplos nativos de um alimentador com ramais de 8 A/12 A e um domínio 24 Vcc independente, além de motor trifásico através de disjuntor/contator/relé térmico e continuações entre folhas. São **dados didáticos**, inclusive os envelopes e associações de proteção; não são um catálogo nem parâmetros para copiar para uma instalação real.
