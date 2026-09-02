/**
 * @file tarot_deck.c
 * @brief Baralho "KIT Tarot" — 78 cartas. Texto original, CC0 (ver README.md).
 *
 * 0–21  Arcanos Maiores (numeração Rider–Waite–Smith: VIII Força, XI Justiça)
 * 22–35 Paus · Fogo      36–49 Copas · Água
 * 50–63 Espadas · Ar     64–77 Ouros · Terra
 *
 * `tarot_deck_count` deriva de sizeof; o sorteio (tarot_draw.c) opera sobre
 * ele, então basta manter o array consistente.
 */
#include "tarot_deck.h"

const tarot_card_t tarot_deck[] = {

/* ======================= ARCANOS MAIORES (0–XXI) ======================= */

{
    .name     = "O Louco",
    .arcana   = "0 · Arcano Maior",
    .about    = "O ponto de partida: um salto no escuro movido por fé e "
                "curiosidade, sem bagagem e sem garantias.",
    .keywords = "Início · Fé · Liberdade",
    .upright  = "Um começo espontâneo pede coragem para dar o primeiro passo "
                "mesmo sem ver o caminho todo. Confie no impulso, mas não "
                "ignore a beira do penhasco.",
    .reversed = "O impulso virou imprudência, ou o medo travou o salto. Ou "
                "você pula sem olhar, ou hesita demais. Falta um mínimo de "
                "plano.",
    .as_past    = "Uma escolha ousada e ingênua abriu a estrada em que você está.",
    .as_present = "Você está diante de um salto: partir leve ou ficar.",
    .as_future  = "Um convite a recomeçar do zero, com mais leveza do que imagina.",
},
{
    .name     = "O Mago",
    .arcana   = "I · Arcano Maior",
    .about    = "A figura que transforma intenção em ação: tem todas as "
                "ferramentas sobre a mesa e sabe usá-las.",
    .keywords = "Poder · Foco · Ação",
    .upright  = "Você tem recursos, talento e o momento a favor. Falta "
                "concentrar tudo num objetivo claro e agir — vontade que "
                "vira realização.",
    .reversed = "Talento desperdiçado, energia dispersa ou manipulação. As "
                "ferramentas estão aí, mas usadas para iludir os outros ou "
                "a si mesmo.",
    .as_past    = "Um momento em que você reuniu o que tinha e fez acontecer.",
    .as_present = "Os elementos estão na mesa; agora depende da sua intenção.",
    .as_future  = "Uma janela para agir com foco total — não a desperdice.",
},
{
    .name     = "A Sacerdotisa",
    .arcana   = "II · Arcano Maior",
    .about    = "A guardiã do conhecimento interior: intuição, mistério e o "
                "que só se sabe por dentro.",
    .keywords = "Intuição · Silêncio · Mistério",
    .upright  = "Há algo que você já sente mas ainda não sabe explicar. "
                "Escute o silêncio antes de agir; nem tudo precisa ser "
                "resolvido agora.",
    .reversed = "Intuição ignorada ou abafada por ruído externo. Segredos "
                "que pesam, ou desconexão do próprio sentir.",
    .as_past    = "Um conhecimento silencioso te guiou sem alarde.",
    .as_present = "A resposta está dentro, não fora — pare para ouvi-la.",
    .as_future  = "Algo vai se revelar no tempo certo; por ora, observe.",
},
{
    .name     = "A Imperatriz",
    .arcana   = "III · Arcano Maior",
    .about    = "A força que cria, nutre e faz crescer: abundância, natureza, "
                "corpo e afeto.",
    .keywords = "Criação · Cuidado · Abundância",
    .upright  = "Um projeto, vínculo ou ideia está fértil e pede cuidado "
                "para florescer. Prazer, generosidade e conexão com o "
                "concreto.",
    .reversed = "Bloqueio criativo, zelo que sufoca, ou descuido consigo. "
                "Dar demais aos outros e nada a si.",
    .as_past    = "Um período fértil gerou algo que você ainda colhe.",
    .as_present = "Algo sob seus cuidados está crescendo — regue com calma.",
    .as_future  = "Uma fase de fartura e criação se aproxima.",
},
{
    .name     = "O Imperador",
    .arcana   = "IV · Arcano Maior",
    .about    = "A ordem, a estrutura e a autoridade: regras, limites e "
                "responsabilidade.",
    .keywords = "Estrutura · Ordem · Autoridade",
    .upright  = "É hora de organizar, definir regras e assumir o comando "
                "com firmeza. Estabilidade construída com disciplina.",
    .reversed = "Rigidez e autoritarismo, ou o oposto: falta de rumo e de "
                "limites. Controle que virou teimosia.",
    .as_past    = "Uma base sólida — ou uma autoridade — moldou onde você está.",
    .as_present = "A situação pede estrutura e uma decisão firme sua.",
    .as_future  = "Estabilidade à vista, desde que você construa o alicerce.",
},
{
    .name     = "O Hierofante",
    .arcana   = "V · Arcano Maior",
    .about    = "A tradição, a instituição e o mestre: o conhecimento que se "
                "aprende com os outros e com o que já existe.",
    .keywords = "Tradição · Aprendizado · Pertencer",
    .upright  = "Buscar orientação em quem já trilhou o caminho, ou seguir "
                "um método comprovado. Valores compartilhados, pertencer a "
                "um grupo.",
    .reversed = "Questionar dogmas e regras herdadas. O convencional já não "
                "serve; é hora de achar o próprio caminho.",
    .as_past    = "Uma formação, fé ou instituição deu forma aos seus valores.",
    .as_present = "Vale procurar um mentor ou um caminho já testado.",
    .as_future  = "Um aprendizado formal ou um rito de passagem se aproxima.",
},
{
    .name     = "Os Amantes",
    .arcana   = "VI · Arcano Maior",
    .about    = "A escolha do coração e a união: vínculos, valores e a "
                "decisão entre caminhos.",
    .keywords = "Escolha · União · Valores",
    .upright  = "Uma conexão significativa, ou uma decisão importante "
                "alinhada ao que você realmente valoriza. Harmonia entre "
                "as partes.",
    .reversed = "Desalinhamento de valores, indecisão ou uma relação em "
                "desequilíbrio. Escolha feita de fora, não de dentro.",
    .as_past    = "Uma escolha afetiva ou de valores te trouxe até aqui.",
    .as_present = "Você está diante de uma decisão que precisa vir do coração.",
    .as_future  = "Uma união ou uma escolha definidora se aproxima.",
},
{
    .name     = "O Carro",
    .arcana   = "VII · Arcano Maior",
    .about    = "A vitória pelo esforço e pela direção: avançar apesar de "
                "forças que puxam para lados opostos.",
    .keywords = "Vontade · Direção · Conquista",
    .upright  = "Determinação e autocontrole levam você adiante. Segure as "
                "rédeas das forças contrárias e siga com foco.",
    .reversed = "Falta de rumo, forças internas em conflito, ou avanço na "
                "marra sem direção. O carro anda em círculos.",
    .as_past    = "Um esforço disciplinado te fez superar um obstáculo.",
    .as_present = "Segure as rédeas: dá para vencer se você mantiver a direção.",
    .as_future  = "Uma conquista vem, fruto de vontade e controle.",
},
{
    .name     = "A Força",
    .arcana   = "VIII · Arcano Maior",
    .about    = "O poder que age pela calma, não pela brutalidade: coragem "
                "serena e domínio dos próprios impulsos.",
    .keywords = "Coragem · Paciência · Domínio",
    .upright  = "Enfrentar um desafio com paciência e firmeza gentil, não "
                "com força bruta. A coragem tranquila vence o medo.",
    .reversed = "Impaciência, dúvida na própria capacidade, ou raiva no "
                "comando. A força vira dureza — ou desânimo.",
    .as_past    = "Você domou uma situação difícil com paciência, não na força.",
    .as_present = "O desafio pede firmeza gentil e sangue-frio.",
    .as_future  = "Uma prova de coragem serena está por vir.",
},
{
    .name     = "O Eremita",
    .arcana   = "IX · Arcano Maior",
    .about    = "A busca solitária por sentido: recolhimento, reflexão e a "
                "luz que se acende por dentro.",
    .keywords = "Reflexão · Solidão · Busca",
    .upright  = "Um tempo de afastamento para pensar, buscar respostas "
                "dentro e enxergar com clareza. A sabedoria vem do silêncio.",
    .reversed = "Isolamento em excesso, solidão que dói, ou recusa a olhar "
                "para dentro. Fugir do mundo em vez de refletir.",
    .as_past    = "Um período de recolhimento te deu uma clareza que ainda usa.",
    .as_present = "Afaste-se do barulho: a resposta pede introspecção.",
    .as_future  = "Um tempo de retiro e reflexão se aproxima.",
},
{
    .name     = "A Roda da Fortuna",
    .arcana   = "X · Arcano Maior",
    .about    = "O ciclo que sempre gira: sorte, mudança e o que não está "
                "sob o seu controle.",
    .keywords = "Ciclo · Mudança · Destino",
    .upright  = "Uma virada de ciclo, muitas vezes para melhor. O que "
                "estava parado se move; aceite que nem tudo depende de você.",
    .reversed = "Resistir a uma mudança inevitável, ou uma fase de azar que "
                "insiste. A roda travou — ou está descendo.",
    .as_past    = "Uma reviravolta fora do seu controle mudou o rumo.",
    .as_present = "O ciclo está girando agora; flua com ele.",
    .as_future  = "Uma mudança de sorte está a caminho.",
},
{
    .name     = "A Justiça",
    .arcana   = "XI · Arcano Maior",
    .about    = "A verdade, a causa e efeito e a decisão imparcial: cada "
                "ação tem a sua consequência.",
    .keywords = "Verdade · Equilíbrio · Consequência",
    .upright  = "Uma decisão justa, um acerto de contas, ou a verdade vindo "
                "à tona. Assuma responsabilidade pelo que fez e colheu.",
    .reversed = "Injustiça, negação da própria parcela de culpa, ou decisão "
                "enviesada. A balança está torta.",
    .as_past    = "Uma decisão ou consequência justa te trouxe até aqui.",
    .as_present = "A situação pede honestidade e responsabilidade agora.",
    .as_future  = "Uma verdade virá à tona e as contas se acertarão.",
},
{
    .name     = "O Enforcado",
    .arcana   = "XII · Arcano Maior",
    .about    = "A pausa e a rendição: parar, se entregar e enxergar tudo "
                "de um ângulo invertido.",
    .keywords = "Pausa · Entrega · Perspectiva",
    .upright  = "Um período de espera imposta em que a saída é mudar o "
                "ponto de vista, não forçar. Render-se para entender.",
    .reversed = "Resistência inútil a uma pausa necessária, ou martírio — "
                "ficar pendurado por comodismo. Estagnação disfarçada de "
                "sacrifício.",
    .as_past    = "Uma pausa forçada te fez ver as coisas de outro jeito.",
    .as_present = "Nada de forçar agora: aceite a espera e mude o ângulo.",
    .as_future  = "Um tempo de suspensão vai pedir paciência e nova perspectiva.",
},
{
    .name     = "A Morte",
    .arcana   = "XIII · Arcano Maior",
    .about    = "O fim que abre espaço: transformação profunda — algo "
                "termina para que outra coisa possa começar.",
    .keywords = "Fim · Transformação · Renovação",
    .upright  = "Um ciclo chega ao fim de forma irreversível. Doloroso, mas "
                "necessário — deixe ir o que já acabou.",
    .reversed = "Agarrar-se ao que já morreu, adiar um fim inevitável. A "
                "transformação está travada e apodrece.",
    .as_past    = "Um encerramento importante limpou o terreno para o agora.",
    .as_present = "Algo precisa terminar de vez para você seguir.",
    .as_future  = "Uma transformação profunda se aproxima; não a tema.",
},
{
    .name     = "A Temperança",
    .arcana   = "XIV · Arcano Maior",
    .about    = "O equilíbrio pela mistura certa: paciência, moderação e a "
                "arte de combinar opostos.",
    .keywords = "Equilíbrio · Paciência · Medida",
    .upright  = "Encontrar o meio-termo, dosar, integrar partes que "
                "pareciam incompatíveis. Uma cura que vem devagar.",
    .reversed = "Excesso, pressa ou desequilíbrio. Tentar misturar tudo de "
                "uma vez, ou viver nos extremos.",
    .as_past    = "Um processo paciente de ajuste te trouxe a um bom ponto.",
    .as_present = "A situação pede moderação e a dose certa de cada coisa.",
    .as_future  = "Um período de reequilíbrio e cura gradual vem aí.",
},
{
    .name     = "O Diabo",
    .arcana   = "XV · Arcano Maior",
    .about    = "A amarra que você mesmo alimenta: vícios, medos e apegos "
                "que parecem prisões — mas a corrente é frouxa.",
    .keywords = "Apego · Ilusão · Prisão",
    .upright  = "Um padrão que te prende: um vício, uma relação tóxica, o "
                "medo, o dinheiro. Você tem mais escolha do que admite.",
    .reversed = "O início de uma libertação — perceber a corrente e "
                "afrouxá-la. Ou, ao contrário, negar que ela existe.",
    .as_past    = "Uma amarra antiga — medo, hábito, dependência — moldou o hoje.",
    .as_present = "Olhe para o que te prende: a corrente está mais solta do que parece.",
    .as_future  = "Uma tentação ou apego vai testar a sua liberdade.",
},
{
    .name     = "A Torre",
    .arcana   = "XVI · Arcano Maior",
    .about    = "O desmoronamento súbito: uma estrutura falsa cai de uma "
                "vez, sem aviso e sem pedir licença.",
    .keywords = "Ruptura · Choque · Verdade",
    .upright  = "Algo construído sobre base falsa rui de repente. Choca, "
                "mas derruba a ilusão e libera o terreno.",
    .reversed = "Uma crise adiada que só cresce, ou um colapso que você "
                "atravessa sem aprender nada. O medo da queda maior que a queda.",
    .as_past    = "Um baque repentino derrubou algo e mudou tudo.",
    .as_present = "Uma verdade está derrubando o que não se sustentava.",
    .as_future  = "Uma ruptura súbita vem — desconfortável, mas necessária.",
},
{
    .name     = "A Estrela",
    .arcana   = "XVII · Arcano Maior",
    .about    = "A esperança depois da tempestade: fé renovada, calma e a "
                "sensação de que vai dar certo.",
    .keywords = "Esperança · Fé · Renovação",
    .upright  = "Depois de um período duro, voltam a serenidade e a "
                "confiança no futuro. Cure-se sem pressa; a luz voltou.",
    .reversed = "Desânimo, fé abalada, ou a sensação de que a luz não "
                "chega. Esperança que virou expectativa frustrada.",
    .as_past    = "Um momento de renovação e fé te reergueu.",
    .as_present = "Respire: há mais motivo para esperança do que você sente.",
    .as_future  = "Um período de calma e cura se aproxima.",
},
{
    .name     = "A Lua",
    .arcana   = "XVIII · Arcano Maior",
    .about    = "O território do que não se vê claro: medos, sonhos, "
                "intuição e ilusões que distorcem o caminho.",
    .keywords = "Ilusão · Medo · Intuição",
    .upright  = "Nem tudo é o que parece. Ande devagar, confie no instinto "
                "e não tome decisões grandes na névoa.",
    .reversed = "A névoa começa a se dissipar — ou, ao contrário, a "
                "confusão e a ansiedade aumentam. Medos saindo à luz.",
    .as_past    = "Uma fase confusa e cheia de medos deixou marcas.",
    .as_present = "Você está na névoa: não force uma clareza que ainda não existe.",
    .as_future  = "Um período de incerteza pede cautela e escuta do instinto.",
},
{
    .name     = "O Sol",
    .arcana   = "XIX · Arcano Maior",
    .about    = "A clareza e a alegria plena: vitalidade, sucesso e as "
                "coisas finalmente à luz do dia.",
    .keywords = "Alegria · Clareza · Sucesso",
    .upright  = "Um momento de vitalidade, êxito e verdade exposta. As "
                "coisas dão certo e você pode celebrar sem culpa.",
    .reversed = "Otimismo forçado, um brilho que não chega, ou um sucesso "
                "que parece vazio. A luz está lá, só um pouco encoberta.",
    .as_past    = "Um período luminoso e bem-sucedido te deu força.",
    .as_present = "Aproveite: é um momento de clareza e energia alta.",
    .as_future  = "Dias mais leves e bem-sucedidos vêm aí.",
},
{
    .name     = "O Julgamento",
    .arcana   = "XX · Arcano Maior",
    .about    = "O chamado para renascer: um acerto de contas com o passado "
                "que permite recomeçar em outro patamar.",
    .keywords = "Despertar · Balanço · Recomeço",
    .upright  = "Um momento de avaliar a própria trajetória, perdoar e "
                "responder a um chamado maior. Um renascimento consciente.",
    .reversed = "Autocrítica dura demais, culpa que trava, ou ignorar um "
                "chamado claro. Um balanço mal feito.",
    .as_past    = "Um ponto de virada te fez repensar quem você é.",
    .as_present = "É hora de fazer as pazes com o passado e responder a um chamado.",
    .as_future  = "Um despertar vai te convidar a recomeçar em outro nível.",
},
{
    .name     = "O Mundo",
    .arcana   = "XXI · Arcano Maior",
    .about    = "A conclusão e a integração: um ciclo se completa por "
                "inteiro — e com ele, uma sensação de plenitude.",
    .keywords = "Conclusão · Plenitude · Integração",
    .upright  = "Algo se fecha com êxito e sentido de totalidade. Colheita, "
                "reconhecimento e o fim de uma longa jornada.",
    .reversed = "Um ciclo quase completo que não fecha, ou a dificuldade de "
                "reconhecer que já chegou. Falta o último passo.",
    .as_past    = "Uma jornada longa se completou e te formou.",
    .as_present = "Você está fechando um ciclo — reconheça o quanto andou.",
    .as_future  = "Uma conclusão plena e merecida se aproxima.",
},

/* ===================== ARCANOS MENORES ================================= *
 * Paus · Fogo (22–35) · Copas · Água (36–49) ·
 * Espadas · Ar (50–63) · Ouros · Terra (64–77)
 * -------------------------------------------------------------------------- */

/* -------------------------- PAUS · Fogo (22–35) -------------------------- */

{
    .name     = "Ás de Paus",
    .arcana   = "Paus · Fogo",
    .about    = "A faísca: um impulso criativo bruto, uma vontade nova de "
                "fazer algo acontecer — energia pura à espera de forma.",
    .keywords = "Faísca · Impulso · Potencial",
    .upright  = "Uma ideia ou desejo acende com força. É o convite a "
                "começar enquanto a empolgação é grande — aja antes que a "
                "faísca esfrie.",
    .reversed = "A faísca não pega: começo adiado, entusiasmo que murcha, "
                "ou energia espalhada em direções demais. Falta um foco "
                "para a vontade.",
    .as_past    = "Um impulso inicial forte deu origem ao que você vive hoje.",
    .as_present = "Há uma faísca acesa agora — dê forma a ela antes que esfrie.",
    .as_future  = "Uma ideia ou vontade nova vai pedir passagem em breve.",
},
{
    .name     = "Dois de Paus",
    .arcana   = "Paus · Fogo",
    .about    = "O primeiro plano: você já tem algo nas mãos e olha para o "
                "horizonte, decidindo se sai da zona de conforto.",
    .keywords = "Plano · Horizonte · Decisão",
    .upright  = "Momento de planejar o próximo passo e escolher entre o "
                "seguro e o maior. O mundo está aberto; falta decidir "
                "partir.",
    .reversed = "O medo do desconhecido trava o plano. Ficar no conhecido "
                "por comodismo, ou planejar mal e sair sem preparo.",
    .as_past    = "Uma decisão de expandir horizontes te trouxe até aqui.",
    .as_present = "Você está no ponto de escolher: ficar seguro ou ir além.",
    .as_future  = "Uma decisão sobre ampliar seus planos se aproxima.",
},
{
    .name     = "Três de Paus",
    .arcana   = "Paus · Fogo",
    .about    = "A espera ativa: o plano saiu do papel e agora você aguarda "
                "o retorno, olhando longe enquanto as coisas se movem.",
    .keywords = "Expansão · Visão · Espera",
    .upright  = "O primeiro esforço já rendeu e novas oportunidades se "
                "aproximam. Continue olhando adiante; o que você plantou "
                "está a caminho.",
    .reversed = "Atrasos no que você esperava, ou visão curta demais. "
                "Planos que não decolam porque falta paciência ou alcance.",
    .as_past    = "Um plano posto em marcha começou a dar frutos.",
    .as_present = "O que você iniciou está em trânsito — mantenha o olhar longe.",
    .as_future  = "Um retorno do seu esforço se aproxima; a expansão continua.",
},
{
    .name     = "Quatro de Paus",
    .arcana   = "Paus · Fogo",
    .about    = "A comemoração: um marco alcançado, estabilidade, casa e "
                "gente ao redor — um momento de dizer \"chegamos\".",
    .keywords = "Marco · Casa · Celebração",
    .upright  = "Uma conquista digna de festejar, sozinho ou com quem "
                "importa. Sensação de base firme e de pertencer.",
    .reversed = "A festa não sai como esperado, ou falta o apoio de quem "
                "você conta. Instabilidade em casa, ou celebrar só por "
                "dentro.",
    .as_past    = "Um marco comemorado te deu uma base que ainda sustenta você.",
    .as_present = "Há motivo para celebrar agora — reconheça o que já foi feito.",
    .as_future  = "Um marco e um momento de festa se aproximam.",
},
{
    .name     = "Cinco de Paus",
    .arcana   = "Paus · Fogo",
    .about    = "O bate-boca: muitas vontades disputando espaço ao mesmo "
                "tempo — atrito, competição, briga sem grande dano.",
    .keywords = "Conflito · Disputa · Atrito",
    .upright  = "Divergências e competição pedem sua energia. É desgastante, "
                "mas o embate pode afinar suas ideias se você não levar "
                "para o pessoal.",
    .reversed = "O conflito se encerra, ou você o evita. Também pode ser "
                "uma briga interna: vontades suas puxando para lados "
                "diferentes.",
    .as_past    = "Um período de disputa e atrito moldou como você se posiciona.",
    .as_present = "Você está no meio de um bate-boca — escolha suas batalhas.",
    .as_future  = "Uma fase de competição ou divergência se aproxima.",
},
{
    .name     = "Seis de Paus",
    .arcana   = "Paus · Fogo",
    .about    = "A vitória reconhecida: o esforço deu certo e os outros "
                "veem — reconhecimento público, cabeça erguida.",
    .keywords = "Vitória · Reconhecimento · Orgulho",
    .upright  = "Uma conquista ganha visibilidade e aplauso merecido. "
                "Aproveite o bom momento sem deixar subir à cabeça.",
    .reversed = "Reconhecimento que não vem, ou vitória que fica só sua. "
                "Também: ego inflado, ou medo de decepcionar quem torce.",
    .as_past    = "Um sucesso reconhecido te deu confiança para seguir.",
    .as_present = "Um bom resultado está à vista — deixe que vejam.",
    .as_future  = "Um reconhecimento pelo seu esforço se aproxima.",
},
{
    .name     = "Sete de Paus",
    .arcana   = "Paus · Fogo",
    .about    = "A defesa da posição: você conquistou um lugar e agora "
                "precisa segurá-lo contra quem quer o mesmo.",
    .keywords = "Defesa · Firmeza · Resistência",
    .upright  = "Vale a pena manter sua posição e defender o que você "
                "construiu, mesmo cansado. Você tem a vantagem de já estar "
                "lá.",
    .reversed = "Cansaço de tanto se defender, vontade de largar tudo, ou "
                "sentir-se cercado. A pressão parece maior do que é.",
    .as_past    = "Um período de defender sua posição te deixou mais firme.",
    .as_present = "Segure o que é seu: você está por cima, mesmo sob pressão.",
    .as_future  = "Um desafio vai pedir que você defenda seu terreno.",
},
{
    .name     = "Oito de Paus",
    .arcana   = "Paus · Fogo",
    .about    = "A aceleração: tudo se move rápido de uma vez — notícias, "
                "respostas, coisas que estavam paradas destravando.",
    .keywords = "Rapidez · Movimento · Notícia",
    .upright  = "As coisas ganham velocidade e se encaixam depressa. "
                "Momento de agir na hora, não de deliberar — a janela é "
                "curta.",
    .reversed = "Freio brusco, atrasos, mensagens que não chegam. A pressa "
                "vira frustração, ou você resiste a um ritmo que não "
                "escolheu.",
    .as_past    = "Uma fase acelerada mudou tudo em pouco tempo.",
    .as_present = "As coisas estão se movendo rápido — acompanhe o ritmo.",
    .as_future  = "Um período de movimento rápido e novidades se aproxima.",
},
{
    .name     = "Nove de Paus",
    .arcana   = "Paus · Fogo",
    .about    = "O último fôlego: você já apanhou bastante, está quase lá, "
                "e monta guarda mesmo exausto.",
    .keywords = "Resiliência · Cansaço · Guarda",
    .upright  = "Falta pouco. As feridas do caminho deixam você "
                "desconfiado, mas aguentar mais um round costuma valer a "
                "pena.",
    .reversed = "Exaustão que pede pausa, desistir a um passo do fim, ou "
                "defesa exagerada — desconfiar de tudo e de todos.",
    .as_past    = "Uma série de embates te deixou resistente, mas na defensiva.",
    .as_present = "Você está quase lá e cansado — mais um esforço, com pausa depois.",
    .as_future  = "Uma reta final exigente vai testar sua persistência.",
},
{
    .name     = "Dez de Paus",
    .arcana   = "Paus · Fogo",
    .about    = "O fardo: responsabilidade demais nas costas de uma pessoa "
                "só — trabalho que sobrecarrega.",
    .keywords = "Peso · Sobrecarga · Dever",
    .upright  = "Você está carregando mais do que deveria. A meta está "
                "perto, mas repense o que dá para largar, dividir ou dizer "
                "não.",
    .reversed = "Largar um peso, delegar, ou chegar ao limite e desabar. "
                "Também: recusar uma responsabilidade que não é sua.",
    .as_past    = "Um período de sobrecarga te trouxe aqui — e cobrou o preço.",
    .as_present = "Você está carregando demais — veja o que pode soltar.",
    .as_future  = "Uma fase de muita responsabilidade vem aí; prepare o fôlego.",
},
{
    .name     = "Valete de Paus",
    .arcana   = "Paus · Fogo",
    .about    = "O explorador: alguém — ou uma parte de você — cheio de "
                "entusiasmo por uma ideia nova, ainda sem método.",
    .keywords = "Entusiasmo · Descoberta · Ímpeto",
    .upright  = "Uma vontade nova pede espaço para ser explorada. Siga a "
                "curiosidade e experimente, sem cobrar de si um plano "
                "completo.",
    .reversed = "Entusiasmo que se dispersa, pressa sem preparo, ou adiar o "
                "começo. Ideias demais e nenhuma levada adiante.",
    .as_past    = "Uma curiosidade nova acendeu algo que você desenvolveu depois.",
    .as_present = "Há uma vontade nova pedindo para ser experimentada.",
    .as_future  = "Uma ideia empolgante vai chamar sua atenção em breve.",
},
{
    .name     = "Cavaleiro de Paus",
    .arcana   = "Paus · Fogo",
    .about    = "O ímpeto em ação: energia, paixão e coragem que avançam de "
                "peito aberto — às vezes rápido demais.",
    .keywords = "Ímpeto · Paixão · Aventura",
    .upright  = "É hora de agir com ousadia e seguir o que te move. A "
                "energia está alta; use-a antes que vire só agitação.",
    .reversed = "Pressa que atropela, promessas maiores que a entrega, ou "
                "frustração de energia sem direção. Muito falar, pouco "
                "fazer.",
    .as_past    = "Um lance de ousadia e energia definiu o rumo atual.",
    .as_present = "A situação pede ação corajosa — mas não atropele o caminho.",
    .as_future  = "Um período de movimento intenso e aventura se aproxima.",
},
{
    .name     = "Rainha de Paus",
    .arcana   = "Paus · Fogo",
    .about    = "A presença que aquece: confiança, carisma e determinação "
                "que atraem os outros sem esforço.",
    .keywords = "Confiança · Carisma · Calor",
    .upright  = "Aja com segurança e generosidade, mostrando quem você é "
                "sem pedir licença. Sua presença abre portas agora.",
    .reversed = "Insegurança disfarçada de dureza, ciúme, ou se cobrar "
                "demais. O calor vira exigência, ou você se recolhe.",
    .as_past    = "Um período de confiança e presença forte te trouxe até aqui.",
    .as_present = "Ocupe seu espaço com segurança e calor — as pessoas respondem.",
    .as_future  = "Um momento de brilhar pela sua presença se aproxima.",
},
{
    .name     = "Rei de Paus",
    .arcana   = "Paus · Fogo",
    .about    = "O líder visionário: enxerga longe, decide rápido e inspira "
                "os outros a irem junto.",
    .keywords = "Visão · Liderança · Ousadia",
    .upright  = "Assuma a frente com uma visão clara e convicção. É hora de "
                "liderar pelo exemplo e apostar grande com responsabilidade.",
    .reversed = "Autoritarismo, impaciência com quem não acompanha, ou "
                "expectativas altas demais. Decisão impulsiva no lugar de "
                "estratégia.",
    .as_past    = "Uma liderança clara — sua ou de alguém — moldou o caminho.",
    .as_present = "A situação pede que você lidere com visão e decida com firmeza.",
    .as_future  = "Uma oportunidade de liderar algo maior se aproxima.",
},

/* ------------------------- COPAS · Água (36–49) ------------------------- */

{
    .name     = "Ás de Copas",
    .arcana   = "Copas · Água",
    .about    = "O coração transbordando: um sentimento novo, uma abertura "
                "afetiva — a nascente de amor, compaixão ou inspiração.",
    .keywords = "Abertura · Afeto · Nascente",
    .upright  = "Um vínculo, um perdão ou uma emoção nova pede espaço para "
                "fluir. Deixe o coração se abrir sem já querer controlar o "
                "formato.",
    .reversed = "Emoção travada, coração fechado, ou um vazio que não se "
                "admite. Talvez o afeto que falte seja por você mesmo.",
    .as_past    = "Uma abertura afetiva regou o que você sente hoje.",
    .as_present = "Algo no coração quer transbordar — não segure a torneira.",
    .as_future  = "Um sentimento novo ou uma reconciliação se aproxima.",
},
{
    .name     = "Dois de Copas",
    .arcana   = "Copas · Água",
    .about    = "O encontro: duas pessoas — ou dois lados de você — se "
                "reconhecem e firmam um vínculo de igual para igual.",
    .keywords = "Vínculo · Reciprocidade · Encontro",
    .upright  = "Uma conexão mútua e equilibrada: parceria, reconciliação, "
                "um acordo em que os dois ganham. Trate com cuidado.",
    .reversed = "Desequilíbrio no vínculo, ruído na comunicação, ou um "
                "afastamento. Um dos lados dá mais do que recebe.",
    .as_past    = "Um encontro ou parceria de igual para igual te trouxe até aqui.",
    .as_present = "Um vínculo pede reciprocidade — cuide dos dois lados.",
    .as_future  = "Uma conexão equilibrada ou uma reaproximação se aproxima.",
},
{
    .name     = "Três de Copas",
    .arcana   = "Copas · Água",
    .about    = "A celebração entre amigos: gente querida reunida, alegria "
                "compartilhada, o brinde de quem se apoia.",
    .keywords = "Amizade · Comemoração · Comunidade",
    .upright  = "Momento de celebrar com quem importa e se deixar apoiar. A "
                "alegria cresce quando dividida.",
    .reversed = "Exagero na farra, fofoca, ou um terceiro atrapalhando a "
                "dupla. Também: afastar-se dos amigos e sentir falta.",
    .as_past    = "Um período de amizade e celebração deixou boas raízes.",
    .as_present = "Aproxime-se de quem te faz bem — não passe por isso sozinho.",
    .as_future  = "Um encontro alegre com gente querida se aproxima.",
},
{
    .name     = "Quatro de Copas",
    .arcana   = "Copas · Água",
    .about    = "O tédio afetivo: olhar para dentro e não ver graça em "
                "nada, ignorando uma oferta que está bem ali.",
    .keywords = "Apatia · Tédio · Retração",
    .upright  = "Uma fase de desânimo e desinteresse. Antes de recusar "
                "tudo, veja o que a vida está te oferecendo e você não "
                "reparou.",
    .reversed = "A apatia começa a passar. Volta a curiosidade, e uma "
                "oferta antes ignorada ganha outra cor.",
    .as_past    = "Um período de desânimo e retração marcou como você se sente.",
    .as_present = "Você está entediado — mas há uma oferta que passou batida.",
    .as_future  = "Uma fase morna vai pedir que você reabra os olhos.",
},
{
    .name     = "Cinco de Copas",
    .arcana   = "Copas · Água",
    .about    = "O luto pelo que virou: olhar para o que se perdeu e não "
                "ver que ainda sobrou algo de pé.",
    .keywords = "Perda · Pesar · Arrependimento",
    .upright  = "Uma decepção ou perda merece ser sentida. Mas não fique só "
                "no que caiu: parte do que importava ainda está aí.",
    .reversed = "O peso da perda começa a aliviar. Aceitação, perdão e a "
                "força para virar de frente para o que restou.",
    .as_past    = "Uma perda ou decepção antiga ainda pesa no presente.",
    .as_present = "Você está de luto por algo — sinta, mas olhe o que ficou.",
    .as_future  = "Um período de aceitar uma perda e seguir se aproxima.",
},
{
    .name     = "Seis de Copas",
    .arcana   = "Copas · Água",
    .about    = "A nostalgia: memórias de infância, um gesto gentil, o "
                "aconchego do que já foi — às vezes um reencontro.",
    .keywords = "Memória · Infância · Reencontro",
    .upright  = "Um retorno ao que era simples e seguro: revisitar o "
                "passado, um reencontro, ou agir com a generosidade de "
                "criança.",
    .reversed = "Preso na saudade, idealizando um passado que não volta. "
                "Traga o bom de lá para o agora e siga.",
    .as_past    = "Um vínculo ou lugar da infância moldou quem você é.",
    .as_present = "Algo do passado volta agora — aproveite o carinho, sem grudar.",
    .as_future  = "Um reencontro ou uma lembrança viva se aproxima.",
},
{
    .name     = "Sete de Copas",
    .arcana   = "Copas · Água",
    .about    = "As muitas taças no ar: opções demais, fantasias e desejos "
                "misturados, difícil saber o que é real.",
    .keywords = "Opções · Fantasia · Ilusão",
    .upright  = "Você tem várias possibilidades, mas nem todas são o que "
                "parecem. Sonhe, e depois pise no chão para escolher uma.",
    .reversed = "A névoa se dissipa e uma escolha fica clara. Ou o "
                "contrário: paralisia diante de tantas opções.",
    .as_past    = "Um momento de muitas possibilidades e pouca clareza te trouxe aqui.",
    .as_present = "Há opções demais no ar — separe o desejo do possível.",
    .as_future  = "Uma fase de escolhas e devaneios pede pés no chão.",
},
{
    .name     = "Oito de Copas",
    .arcana   = "Copas · Água",
    .about    = "Dar as costas: deixar para trás algo que já não preenche, "
                "mesmo sem saber ao certo o que vem depois.",
    .keywords = "Partida · Busca · Desapego",
    .upright  = "Algo que era importante deixou de bastar. É hora de se "
                "afastar em busca de mais sentido, mesmo que doa um pouco.",
    .reversed = "Ficar por medo de partir, ou ir embora sem rumo. Você "
                "sente que precisa mudar, mas hesita no primeiro passo.",
    .as_past    = "Uma decisão de virar as costas a algo abriu o caminho atual.",
    .as_present = "Algo já não te preenche — talvez seja hora de seguir.",
    .as_future  = "Uma partida em busca de mais sentido se aproxima.",
},
{
    .name     = "Nove de Copas",
    .arcana   = "Copas · Água",
    .about    = "O contentamento: a sensação de que o que você queria se "
                "realizou — conforto, satisfação, o desejo atendido.",
    .keywords = "Satisfação · Conforto · Desejo",
    .upright  = "Um momento de bem-estar e desejo realizado. Aproveite e "
                "agradeça, sem confundir contentamento com ter tudo.",
    .reversed = "Satisfação que soa vazia, ou desejo que não se cumpre. "
                "Também: buscar aprovação de fora para se sentir bem.",
    .as_past    = "Um desejo realizado te deu uma base de bem-estar.",
    .as_present = "Há motivo para se sentir satisfeito agora — reconheça.",
    .as_future  = "Um período de conforto e um desejo atendido se aproximam.",
},
{
    .name     = "Dez de Copas",
    .arcana   = "Copas · Água",
    .about    = "A harmonia afetiva plena: laços em paz, casa em ordem, a "
                "alegria estável de pertencer a algo.",
    .keywords = "Harmonia · Família · Plenitude",
    .upright  = "Um período de vínculos em paz e sensação de lar. O afeto "
                "está alinhado com o que você valoriza.",
    .reversed = "Distância entre o retrato ideal e o real: atrito em casa, "
                "valores desencontrados, ou a alegria só na aparência.",
    .as_past    = "Um período de laços harmoniosos deixou uma base afetiva firme.",
    .as_present = "Seus vínculos estão em boa fase — cuide dessa harmonia.",
    .as_future  = "Um momento de paz nos afetos e sensação de lar se aproxima.",
},
{
    .name     = "Valete de Copas",
    .arcana   = "Copas · Água",
    .about    = "O recado do coração: alguém sensível e imaginativo, uma "
                "mensagem afetuosa, um convite para sentir.",
    .keywords = "Sensibilidade · Imaginação · Convite",
    .upright  = "Uma ideia criativa ou um gesto afetivo pede atenção. "
                "Deixe-se surpreender e responda com o coração aberto.",
    .reversed = "Sensibilidade que vira melindre, criatividade travada, ou "
                "insegurança afetiva. O humor oscila à toa.",
    .as_past    = "Uma mensagem ou vínculo afetivo delicado deixou marca.",
    .as_present = "Há um convite emocional ou criativo — receba com abertura.",
    .as_future  = "Uma novidade afetuosa ou inspiradora se aproxima.",
},
{
    .name     = "Cavaleiro de Copas",
    .arcana   = "Copas · Água",
    .about    = "O romântico em movimento: segue o coração, faz propostas, "
                "chega trazendo um convite ou uma declaração.",
    .keywords = "Romance · Proposta · Idealismo",
    .upright  = "Um convite, uma declaração ou um gesto guiado pelo "
                "sentimento. Avance com o coração, mas mantenha um pé na "
                "realidade.",
    .reversed = "Idealização que decepciona, promessa que não se cumpre, ou "
                "usar a emoção para conseguir algo. Humor instável.",
    .as_past    = "Um gesto romântico ou idealista definiu parte do caminho.",
    .as_present = "Siga o que o coração aponta — sem perder o senso de realidade.",
    .as_future  = "Um convite ou proposta afetiva se aproxima.",
},
{
    .name     = "Rainha de Copas",
    .arcana   = "Copas · Água",
    .about    = "A que acolhe: profundidade emocional, empatia e a calma de "
                "segurar o sentimento dos outros sem se perder.",
    .keywords = "Empatia · Acolhida · Intuição",
    .upright  = "Aja pela escuta e pela compaixão, sua ou pelos outros. Sua "
                "sensibilidade é uma força — confie nela.",
    .reversed = "Absorver emoção demais, se anular pelos outros, ou "
                "insegurança que vira carência. Faltam limites ao cuidado.",
    .as_past    = "Um período de acolher e sentir fundo te formou.",
    .as_present = "A situação pede empatia e escuta — inclusive consigo.",
    .as_future  = "Um momento de cuidar e ser cuidado se aproxima.",
},
{
    .name     = "Rei de Copas",
    .arcana   = "Copas · Água",
    .about    = "O equilíbrio emocional: sente tudo, mas não se afoga — "
                "calma sob pressão, diplomacia, afeto maduro.",
    .keywords = "Serenidade · Diplomacia · Maturidade",
    .upright  = "Lide com a situação pelo tato e pela calma, mesmo em meio "
                "à emoção. Você consegue segurar a onda sem endurecer.",
    .reversed = "Emoção reprimida ou usada para manipular, humor "
                "imprevisível, ou frieza. O controle virou distância.",
    .as_past    = "Uma postura serena diante da emoção te trouxe até aqui.",
    .as_present = "A situação pede calma emocional e diplomacia.",
    .as_future  = "Um momento de conduzir pelo equilíbrio afetivo se aproxima.",
},

/* ------------------------ ESPADAS · Ar (50–63) ------------------------- */

{
    .name     = "Ás de Espadas",
    .arcana   = "Espadas · Ar",
    .about    = "O corte que clareia: uma verdade, uma ideia afiada, a "
                "força mental para atravessar a confusão e ver o essencial.",
    .keywords = "Clareza · Verdade · Corte",
    .upright  = "Um pensamento certeiro ou uma verdade dita na hora abre "
                "caminho. Use a lâmina para separar o que importa do ruído "
                "— com cuidado.",
    .reversed = "Confusão, julgamento nublado, ou informação distorcida. A "
                "verdade dita sem tato corta mais do que devia.",
    .as_past    = "Uma verdade ou decisão certeira definiu o rumo atual.",
    .as_present = "Há uma clareza cortante disponível — nomeie o que você vê.",
    .as_future  = "Uma ideia ou verdade decisiva vai se impor em breve.",
},
{
    .name     = "Dois de Espadas",
    .arcana   = "Espadas · Ar",
    .about    = "O impasse de olhos vendados: duas opções pesando igual, e "
                "a vontade de não olhar para nenhuma.",
    .keywords = "Impasse · Evasão · Escolha",
    .upright  = "Uma decisão difícil está sendo adiada. Você tem mais "
                "informação do que admite — tire a venda e encare o que "
                "precisa ser pesado.",
    .reversed = "O impasse se rompe: um dado novo aparece e a escolha fica "
                "clara. Ou a evasão só aumenta a tensão.",
    .as_past    = "Uma escolha adiada por tempo demais te trouxe até aqui.",
    .as_present = "Você está empatado consigo — tire a venda e decida.",
    .as_future  = "Uma decisão que você vem evitando vai pedir passagem.",
},
{
    .name     = "Três de Espadas",
    .arcana   = "Espadas · Ar",
    .about    = "A dor de uma verdade: mágoa, decepção, o baque de algo que "
                "fere o coração pela cabeça.",
    .keywords = "Mágoa · Ruptura · Dor",
    .upright  = "Uma verdade dolorosa ou uma decepção precisa ser sentida, "
                "não empurrada para baixo. A tempestade passa depois de "
                "cair.",
    .reversed = "A dor começa a escoar: perdão, alívio, cicatriz que sara. "
                "Ou o contrário: segurar a mágoa por tempo demais.",
    .as_past    = "Uma mágoa ou decepção marcante ainda ecoa no presente.",
    .as_present = "Algo dói agora — deixe a chuva cair antes de seguir.",
    .as_future  = "Uma verdade difícil pode machucar antes de aliviar.",
},
{
    .name     = "Quatro de Espadas",
    .arcana   = "Espadas · Ar",
    .about    = "A trégua: parar para se recuperar depois de uma batalha, "
                "em silêncio, sem culpa.",
    .keywords = "Descanso · Recolhimento · Pausa",
    .upright  = "Hora de recolher, dormir, ficar quieto e recuperar as "
                "forças. A pausa não é fuga — é parte da luta.",
    .reversed = "Inquietação que não deixa parar, esgotamento ignorado, ou "
                "o momento de voltar à ativa depois do repouso.",
    .as_past    = "Um período de recolhimento te devolveu as forças.",
    .as_present = "Pare e descanse — o corpo e a mente estão pedindo trégua.",
    .as_future  = "Um tempo necessário de pausa e recuperação se aproxima.",
},
{
    .name     = "Cinco de Espadas",
    .arcana   = "Espadas · Ar",
    .about    = "A vitória que não vale: ganhar a discussão e perder o "
                "resto, ou sair derrotado de um embate feio.",
    .keywords = "Conflito · Custo · Orgulho",
    .upright  = "Um confronto em que vencer custa caro demais — em vínculos "
                "ou em paz. Pergunte se essa briga vale o que ela cobra.",
    .reversed = "Vontade de fazer as pazes e largar o rancor. Ou uma tensão "
                "que insiste em não se resolver.",
    .as_past    = "Um conflito com preço alto deixou marcas em como você briga.",
    .as_present = "Escolha: vencer a discussão ou preservar o que importa.",
    .as_future  = "Um embate à vista pode custar mais do que rende.",
},
{
    .name     = "Seis de Espadas",
    .arcana   = "Espadas · Ar",
    .about    = "A travessia: sair de águas turbulentas rumo a um lugar "
                "mais calmo, aos poucos, ainda sem terra à vista.",
    .keywords = "Transição · Travessia · Alívio",
    .upright  = "Você está deixando para trás uma fase difícil. A melhora é "
                "gradual — não force o barco, deixe a correnteza ajudar.",
    .reversed = "Resistência a mudar, bagagem que não se larga, ou voltar "
                "sempre ao mesmo ponto sem completar a travessia.",
    .as_past    = "Uma travessia difícil te trouxe a um ponto mais calmo.",
    .as_present = "Você está saindo da parte ruim — siga devagar e firme.",
    .as_future  = "Uma passagem para águas mais calmas se aproxima.",
},
{
    .name     = "Sete de Espadas",
    .arcana   = "Espadas · Ar",
    .about    = "A jogada solitária: agir por fora, com esperteza, driblar "
                "o combinado — às vezes fugir de um embate direto.",
    .keywords = "Estratégia · Furtividade · Desvio",
    .upright  = "Um plano que depende de discrição ou de agir sozinho. Pode "
                "ser sagacidade — ou um atalho que você vai ter que "
                "explicar depois.",
    .reversed = "A consciência pesa, o disfarce cai, ou é hora de assumir o "
                "que foi feito por baixo do pano.",
    .as_past    = "Uma jogada esperta ou um segredo definiu parte do caminho.",
    .as_present = "Você está agindo por fora — pese o que fará quando aparecer.",
    .as_future  = "Uma situação vai pedir tática — ou expor uma que já existe.",
},
{
    .name     = "Oito de Espadas",
    .arcana   = "Espadas · Ar",
    .about    = "A prisão imaginada: sentir-se preso e sem saída, quando as "
                "amarras são mais frouxas do que parecem.",
    .keywords = "Bloqueio · Impotência · Medo",
    .upright  = "Você se sente encurralado, mas boa parte da cerca é feita "
                "de medo e de história antiga. Um passo de lado já muda a "
                "vista.",
    .reversed = "As vendas caem e você percebe a saída. Libertação, nova "
                "perspectiva, crenças limitantes se soltando.",
    .as_past    = "Um período de se sentir preso moldou como você reage a limites.",
    .as_present = "Você se sente sem saída — teste as amarras, elas cedem.",
    .as_future  = "Um aperto vai parecer maior do que de fato é.",
},
{
    .name     = "Nove de Espadas",
    .arcana   = "Espadas · Ar",
    .about    = "A angústia da madrugada: preocupação, insônia, a mente "
                "girando no pior cenário no escuro.",
    .keywords = "Ansiedade · Insônia · Aflição",
    .upright  = "A cabeça está martelando medos, muitos maiores na "
                "imaginação do que na vida. Acenda a luz, nomeie o medo, "
                "peça ajuda.",
    .reversed = "A angústia começa a ceder ao encarar o que a alimenta. Ou, "
                "sem isso, o buraco parece fundo demais.",
    .as_past    = "Um período de muita aflição mental deixou o alerta ligado.",
    .as_present = "A mente está no pior cenário — traga o medo para a luz.",
    .as_future  = "Uma fase de preocupação vai pedir cuidado com a cabeça.",
},
{
    .name     = "Dez de Espadas",
    .arcana   = "Espadas · Ar",
    .about    = "O fundo do poço: um fim doloroso, sensação de derrota "
                "total — mas o pior já passou e o dia vem nascendo.",
    .keywords = "Fim · Fundo · Recomeço",
    .upright  = "Algo acabou do jeito mais duro. Não há como piorar a "
                "partir daqui — e isso, por mais estranho, é um alívio. "
                "Comece a virar.",
    .reversed = "A recuperação começa, devagar. Ou a recusa de largar a dor "
                "de algo que já terminou.",
    .as_past    = "Um fim muito duro limpou o terreno para o que veio depois.",
    .as_present = "Você tocou o fundo — o próximo movimento é para cima.",
    .as_future  = "Um ciclo difícil está perto de fechar; o alívio vem depois.",
},
{
    .name     = "Valete de Espadas",
    .arcana   = "Espadas · Ar",
    .about    = "A mente curiosa e alerta: sede de entender, perguntas "
                "afiadas, vontade de falar tudo na lata.",
    .keywords = "Curiosidade · Vigília · Franqueza",
    .upright  = "Hora de investigar, perguntar e dizer o que pensa com "
                "clareza. Cuidado só para a franqueza não virar pressa ou "
                "aspereza.",
    .reversed = "Palavra dita cedo demais, fofoca, cinismo, ou pensamento "
                "espalhado que não vira nada.",
    .as_past    = "Uma fase de muita curiosidade e franqueza te trouxe até aqui.",
    .as_present = "Investigue e fale com clareza — sem atropelar no caminho.",
    .as_future  = "Uma novidade vai despertar sua curiosidade e sua língua.",
},
{
    .name     = "Cavaleiro de Espadas",
    .arcana   = "Espadas · Ar",
    .about    = "O avanço veloz pela ideia: determinação, franqueza e "
                "pressa de agir — de lâmina em riste.",
    .keywords = "Ímpeto · Lógica · Pressa",
    .upright  = "Momento de ir direto ao ponto e agir rápido por uma "
                "convicção. A força é real; cuide para não passar por cima "
                "de tudo.",
    .reversed = "Agressividade, atropelo, discurso afiado sem entrega, ou "
                "energia que se esgota antes de terminar.",
    .as_past    = "Um avanço decidido e direto marcou o rumo atual.",
    .as_present = "Vá direto ao ponto — mas não atropele quem está no caminho.",
    .as_future  = "Uma investida rápida por uma ideia se aproxima.",
},
{
    .name     = "Rainha de Espadas",
    .arcana   = "Espadas · Ar",
    .about    = "O olhar sem véu: honestidade, independência e "
                "discernimento que não se deixa nublar pela emoção.",
    .keywords = "Lucidez · Independência · Limite",
    .upright  = "Aja com clareza e diga a verdade com franqueza, sem "
                "crueldade. Seus limites são legítimos — sustente-os.",
    .reversed = "Frieza, amargura, julgamento duro ou isolamento. A lâmina "
                "da verdade vira arma para ferir.",
    .as_past    = "Um período de lucidez e limites firmes te formou.",
    .as_present = "A situação pede honestidade clara e limites firmes.",
    .as_future  = "Um momento de decidir com a cabeça fria se aproxima.",
},
{
    .name     = "Rei de Espadas",
    .arcana   = "Espadas · Ar",
    .about    = "A autoridade da razão: verdade, justiça e decisões guiadas "
                "por princípios claros, não por conveniência.",
    .keywords = "Razão · Justiça · Princípio",
    .upright  = "Conduza pela lógica, pela ética e pela palavra firme. É "
                "hora de decidir com imparcialidade e defender um "
                "princípio.",
    .reversed = "Uso da razão para dominar, frieza, manipulação de "
                "argumentos, ou rigidez que não escuta ninguém.",
    .as_past    = "Uma decisão guiada por princípios claros moldou o caminho.",
    .as_present = "A situação pede julgamento imparcial e palavra firme.",
    .as_future  = "Um momento de decidir com clareza e princípio se aproxima.",
},

/* ----------------------- OUROS · Terra (64–77) ------------------------ */

{
    .name     = "Ás de Ouros",
    .arcana   = "Ouros · Terra",
    .about    = "A semente concreta: uma oportunidade material ou de "
                "trabalho, um recurso que chega, um começo que dá para "
                "tocar.",
    .keywords = "Oportunidade · Semente · Recurso",
    .upright  = "Surge uma chance sólida — emprego, grana, projeto. Plante "
                "com cuidado: o potencial é real e pede constância para "
                "crescer.",
    .reversed = "Oportunidade que passa, começo adiado, ou mentalidade de "
                "escassez que faz recusar o que é bom.",
    .as_past    = "Uma oportunidade concreta lançou a base do que você tem hoje.",
    .as_present = "Há uma chance sólida na mão — dê os primeiros passos.",
    .as_future  = "Uma oportunidade material ou de trabalho se aproxima.",
},
{
    .name     = "Dois de Ouros",
    .arcana   = "Ouros · Terra",
    .about    = "O malabarismo: equilibrar contas, tarefas e prioridades "
                "que não param de se mover.",
    .keywords = "Equilíbrio · Malabarismo · Ajuste",
    .upright  = "Você está gerenciando várias frentes ao mesmo tempo. Dá "
                "para manter, desde que você se mantenha flexível e não "
                "pegue mais bolas.",
    .reversed = "Bolas no chão, agenda no limite, desorganização. Assumiu "
                "compromissos demais e algo vai cair.",
    .as_past    = "Um período de equilibrar muitas frentes te trouxe até aqui.",
    .as_present = "Você está fazendo malabarismo — não pegue mais nenhuma bola.",
    .as_future  = "Uma fase de equilibrar prioridades e contas se aproxima.",
},
{
    .name     = "Três de Ouros",
    .arcana   = "Ouros · Terra",
    .about    = "O trabalho em conjunto: juntar habilidades, aprender "
                "fazendo e construir algo com outras mãos.",
    .keywords = "Colaboração · Ofício · Construção",
    .upright  = "Um projeto avança pela troca de competências. Peça ajuda, "
                "mostre a sua parte e deixe cada um contribuir com o que "
                "sabe.",
    .reversed = "Falta de sintonia no time, retrabalho, ou insistir em "
                "fazer sozinho o que pedia mais gente.",
    .as_past    = "Uma boa parceria de trabalho ajudou a construir o que você tem.",
    .as_present = "Este é trabalho de mais de uma pessoa — some forças.",
    .as_future  = "Uma colaboração vai fazer um projeto andar.",
},
{
    .name     = "Quatro de Ouros",
    .arcana   = "Ouros · Terra",
    .about    = "O punho fechado: segurar o que se tem com força — "
                "segurança, poupança, controle — às vezes apego demais.",
    .keywords = "Segurança · Retenção · Controle",
    .upright  = "Momento de guardar, consolidar e proteger o que você "
                "construiu. Só cuidado para o cuidado não virar medo de "
                "soltar.",
    .reversed = "Afrouxar o punho: generosidade, gasto necessário. Ou o "
                "oposto: insegurança financeira, ou soltar dinheiro sem "
                "critério.",
    .as_past    = "Um período de segurar firme o que tinha moldou sua relação com bens.",
    .as_present = "Você está segurando com força — veja se é proteção ou medo.",
    .as_future  = "Uma fase de consolidar e proteger recursos se aproxima.",
},
{
    .name     = "Cinco de Ouros",
    .arcana   = "Ouros · Terra",
    .about    = "O frio do lado de fora: aperto material, exclusão, a "
                "sensação de ter ficado para trás — com ajuda perto sem "
                "ser vista.",
    .keywords = "Aperto · Exclusão · Falta",
    .upright  = "Uma fase de dificuldade concreta ou de se sentir de fora. "
                "O apoio existe, mas você precisa erguer a cabeça para "
                "enxergá-lo.",
    .reversed = "As coisas começam a melhorar: ajuda aceita, fim de um "
                "aperto. Ou o buraco ainda se aprofunda.",
    .as_past    = "Um período de aperto ou exclusão ainda pesa no presente.",
    .as_present = "Você se sente de fora — a ajuda existe, procure por ela.",
    .as_future  = "Uma fase apertada vai pedir que você aceite apoio.",
},
{
    .name     = "Seis de Ouros",
    .arcana   = "Ouros · Terra",
    .about    = "A balança do dar e receber: generosidade, apoio, troca — e "
                "a pergunta de quem tem a mão por cima.",
    .keywords = "Generosidade · Troca · Apoio",
    .upright  = "Um fluxo justo de ajuda: dar quando pode, receber quando "
                "precisa. Mantenha a troca equilibrada dos dois lados.",
    .reversed = "Ajuda com corda amarrada, dívida que pesa, ou dependência. "
                "A generosidade vira poder sobre o outro.",
    .as_past    = "Um gesto de apoio — dado ou recebido — marcou o caminho.",
    .as_present = "Há uma troca acontecendo — cheque se ela é equilibrada.",
    .as_future  = "Um momento de dar ou receber ajuda se aproxima.",
},
{
    .name     = "Sete de Ouros",
    .arcana   = "Ouros · Terra",
    .about    = "A pausa para avaliar a plantação: olhar o que cresceu e "
                "decidir se continua regando ou muda de canteiro.",
    .keywords = "Paciência · Avaliação · Prazo",
    .upright  = "Hora de parar e medir o retorno do seu esforço. O "
                "crescimento é lento; avalie com calma antes de mudar de "
                "estratégia.",
    .reversed = "Impaciência, retorno abaixo do esperado, ou largar um "
                "investimento pouco antes de ele render.",
    .as_past    = "Um investimento de longo prazo começou a mostrar resultado.",
    .as_present = "Pare e avalie o que seu esforço rendeu até aqui.",
    .as_future  = "Um momento de colher — ou replanejar — o que foi plantado.",
},
{
    .name     = "Oito de Ouros",
    .arcana   = "Ouros · Terra",
    .about    = "A bancada de trabalho: repetir, lapidar e melhorar uma "
                "habilidade com dedicação e foco.",
    .keywords = "Dedicação · Ofício · Repetição",
    .upright  = "Momento de baixar a cabeça e treinar. A maestria vem da "
                "repetição atenta — cada peça sai melhor que a anterior.",
    .reversed = "Trabalho no automático sem alma, perfeccionismo que trava, "
                "ou esforço aplicado na coisa errada.",
    .as_past    = "Um período de treino dedicado construiu uma habilidade sua.",
    .as_present = "É hora de praticar com foco — a habilidade se lapida fazendo.",
    .as_future  = "Uma fase de aprender um ofício a fundo se aproxima.",
},
{
    .name     = "Nove de Ouros",
    .arcana   = "Ouros · Terra",
    .about    = "O conforto conquistado: independência, um jardim próprio, "
                "o prazer tranquilo de desfrutar o que o esforço deu.",
    .keywords = "Autossuficiência · Conforto · Fruto",
    .upright  = "Momento de desfrutar, com calma e sem culpa, o que você "
                "construiu sozinho. A independência é um bem — aproveite-a.",
    .reversed = "Depender demais dos outros, tropeço financeiro, ou correr "
                "tanto que não sobra tempo de aproveitar.",
    .as_past    = "Um esforço próprio te trouxe a um lugar de conforto.",
    .as_present = "Desfrute o que você construiu — você merece a pausa.",
    .as_future  = "Um período de colher, sozinho, o fruto do seu trabalho.",
},
{
    .name     = "Dez de Ouros",
    .arcana   = "Ouros · Terra",
    .about    = "A casa construída para durar: patrimônio, família, raízes "
                "e uma base que atravessa gerações.",
    .keywords = "Legado · Raízes · Estabilidade",
    .upright  = "Um período de segurança sólida e de longo prazo: família, "
                "patrimônio, tradição. O que você constrói passa a ter "
                "permanência.",
    .reversed = "Instabilidade financeira, briga por herança ou dinheiro na "
                "família, ou uma tradição que já não serve.",
    .as_past    = "Uma base familiar ou patrimonial sustenta onde você está.",
    .as_present = "Você está construindo algo para durar — cuide dos alicerces.",
    .as_future  = "Uma fase de estabilidade sólida e de longo prazo se aproxima.",
},
{
    .name     = "Valete de Ouros",
    .arcana   = "Ouros · Terra",
    .about    = "O estudante prático: curiosidade sobre como as coisas "
                "funcionam, um plano pé no chão, vontade de aprender e "
                "produzir.",
    .keywords = "Estudo · Plano · Constância",
    .upright  = "Uma ideia concreta pede que você estude, planeje e comece "
                "pequeno. O tijolo por tijolo funciona aqui.",
    .reversed = "Procrastinação, plano irreal, ou empolgação que não vira "
                "prática. Aprendizado adiado.",
    .as_past    = "Uma vontade de aprender algo prático deu origem ao que você faz.",
    .as_present = "Há um plano concreto pedindo estudo e um primeiro passo.",
    .as_future  = "Uma oportunidade de aprender ou empreender algo se aproxima.",
},
{
    .name     = "Cavaleiro de Ouros",
    .arcana   = "Ouros · Terra",
    .about    = "O passo firme e repetido: rotina, confiabilidade e "
                "trabalho metódico que leva as coisas até o fim.",
    .keywords = "Rotina · Confiabilidade · Método",
    .upright  = "O avanço vem do arroz com feijão bem feito: constância, "
                "método e paciência. Sem brilho, mas chega lá.",
    .reversed = "Tédio, estagnação, teimosia, ou perfeccionismo que emperra "
                "o progresso. A rotina virou prisão.",
    .as_past    = "Um período de trabalho metódico construiu uma base firme.",
    .as_present = "A situação pede constância e método, não pressa.",
    .as_future  = "Uma fase de progresso lento e seguro se aproxima.",
},
{
    .name     = "Rainha de Ouros",
    .arcana   = "Ouros · Terra",
    .about    = "O cuidado com os pés no chão: acolher, prover e resolver o "
                "prático, cuidando da casa e de si ao mesmo tempo.",
    .keywords = "Cuidado · Praticidade · Equilíbrio",
    .upright  = "Aja com generosidade prática: resolver o concreto, cuidar "
                "de quem precisa e não esquecer de si no processo.",
    .reversed = "Esquecer-se por cuidar dos outros, sufocar com zelo, ou "
                "desequilíbrio entre trabalho e vida.",
    .as_past    = "Um período de cuidar do prático e dos outros te formou.",
    .as_present = "Cuide do concreto e de quem precisa — sem se abandonar.",
    .as_future  = "Um momento de prover e acolher com equilíbrio se aproxima.",
},
{
    .name     = "Rei de Ouros",
    .arcana   = "Ouros · Terra",
    .about    = "A prosperidade estável: recursos, disciplina e a "
                "habilidade de fazer algo crescer e sustentar quem depende "
                "dele.",
    .keywords = "Prosperidade · Disciplina · Provisão",
    .upright  = "Momento de conduzir com solidez: administrar bem, investir "
                "com juízo e ser generoso com o que se tem. Base firme.",
    .reversed = "Ganância, apego ao material, controle pelo dinheiro, ou "
                "decisões financeiras teimosas e arriscadas.",
    .as_past    = "Uma gestão sólida de recursos moldou onde você está.",
    .as_present = "A situação pede administração firme e generosidade com o que há.",
    .as_future  = "Uma fase de prosperidade estável e bem administrada se aproxima.",
},

};

const int tarot_deck_count = (int)(sizeof(tarot_deck) / sizeof(tarot_deck[0]));
