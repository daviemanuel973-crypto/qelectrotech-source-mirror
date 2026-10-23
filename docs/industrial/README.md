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

**Editar dados elétricos das cargas** cadastra dados conhecidos no próprio `.qet`. O dimensionamento tenta carregar os dados quando encontra uma única carga candidata. Percursos com múltiplas cargas são bloqueados para receber uma seção única, pois o alimentador e as derivações podem ter correntes diferentes. Associações ambíguas precisam de revisão; não existe agregação automática geral de alimentadores.

### Pré-dimensionamento

Selecione fios no desenho ou potenciais no rastreador. Informe os dados ausentes, inclusive comprimento físico, e confirme o perfil apresentado. O cálculo não usa o comprimento desenhado. Sem seleção, é possível apenas classificar; não se aplica uma única bitola ao quadro inteiro.

O núcleo calcula corrente/potência/tensão para CA monofásica, CA trifásica equilibrada e CC; considera fatores declarados de temperatura e agrupamento; verifica queda resistiva, Ib/In/Iz e I2 quando informados e verificação adiabática de curto quando corrente, tempo e k são suficientes. O núcleo também permite conversão de potência mecânica com rendimento; a interface de entrada de potência desta versão pede potência elétrica ativa ou corrente de placa.

A seção é gravada somente nos potenciais escolhidos. Propriedades existentes são preservadas, salvo substituição explícita. O registro do cálculo é salvo junto ao fio no `.qet`; desfazer/refazer inclui esse registro. Cor de desenho, isoladamente, não prova que um fio é PE. PE não recebe seção calculada a partir da corrente da carga.

**Limites materiais:** o perfil de capacidade disponível é preliminar, cobre/PVC 70 °C, B1, sem certificação normativa; não há implementação completa de todos os métodos, materiais, harmônicos, partida de motores, coordenação/seletividade e neutro/PE. Os fatores precisam ser declarados conforme a instalação. Relatórios não apresentam o projeto como 100% dimensionado. Não usar a saída como aprovação de projeto executivo.

## Documentação

**Gerar simbologia gráfica e tabelas (PDF)** exporta simbologia vetorial real, materiais, funções, bornes, condutores com origem/destino, bitolas, potenciais/circuitos, referências entre folhas, dimensionamento, propriedades ausentes e inconsistências. **Exportar tabelas CSV** produz os dados tabulares.

**Gerar documentação em folhas editáveis** acrescenta folhas nativas ao projeto com textos e formas editáveis. A legenda usa gravações vetoriais do próprio símbolo em formas gráficas, sem criar componentes elétricos nem novas unidades de material. A geração pode ser desfeita/refeita em um único lote. As folhas são uma fotografia dos dados no momento da geração; depois de editar o projeto, regenere as folhas. Não são tabelas com atualização automática contínua.

Produtos só são agrupados quando fabricante, código e modelo permitem identificar equivalência. Ocorrências sem fabricante/código permanecem separadas. Contatos escravos vinculados a um mestre não viram produtos físicos adicionais. Dados comerciais ausentes permanecem pendentes.

O projeto original continua abrindo no formato `.qet`. Formas documentais com o campo adicional `qet_symbol_vector` precisam desta versão para exibir o pictograma; versões anteriores do QET podem mostrar apenas o contorno da forma. PDF preserva a representação vetorial.

## Referências e validação

Consulta de edição em 23/10/2026: catálogo ABNT apresenta **ABNT NBR 5410:2004, Versão Corrigida:2008**; IEC apresenta **IEC 60364-5-52:2009+AMD1:2024**. Confirmar a edição não certifica os cálculos desta versão. O perfil herdado não foi convertido em uma implementação normativa completa.

- https://www.abntcatalogo.com.br/
- https://webstore.iec.ch/en/publication/103734

A compilação Windows executa testes matemáticos, integração com classes reais QET (seleção, gravação, geometria, salvar/reabrir, desfazer/refazer, editor de bornes e documentos) e a suíte CTest. O pacote é verificado abrindo um `.qet` com o plugin Qt Windows e PATH sem MSYS2. Verifique o resultado da execução correspondente ao commit. A verificação automatizada de abertura não equivale a aceitação manual completa da GUI no computador do usuário.

A fixture industrial de integração vem do próprio QET: 50 folhas, 618 elementos e 671 fios. Seus dados são de exemplo, não do quadro do usuário. O PDF `DOCUMENTAÇÃO DO QUADRO.pdf` é referência visual; seus dados não são inseridos automaticamente na demonstração.
